/*
** Taiga
** Copyright (C) 2010-2018, Eren Okka
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>

#include "base/file.h"
#include "base/log.h"
#include "base/string.h"
#include "base/xml.h"
#include "library/anime_db.h"
#include "library/anime_item.h"
#include "library/anime_season.h"
#include "library/anime_util.h"
#include "library/discover.h"
#include "sync/manager.h"
#include "sync/service.h"
#include "taiga/http.h"
#include "taiga/path.h"
#include "taiga/settings.h"
#include "taiga/taiga.h"
#include "ui/dlg/dlg_season.h"
#include "ui/ui.h"

library::SeasonDatabase SeasonDatabase;

namespace library {

SeasonDatabase::SeasonDatabase()
    : available_seasons({anime::Season::kWinter, 2011},
                        {anime::Season::kSpring, 2018}),
      remote_location(L"https://raw.githubusercontent.com"
                      L"/erengy/anime-seasons/master/data/") {
}

bool SeasonDatabase::LoadSeason(const anime::Season& season) {
  std::wstring filename =
      ToWstr(season.year) + L"_" + ToLower_Copy(season.GetName()) + L".xml";

  return LoadFile(filename);
}

bool SeasonDatabase::LoadFile(const std::wstring& filename) {
  std::wstring path = taiga::GetPath(taiga::Path::DatabaseSeason) + filename;

  std::string document;

  if (!ReadFromFile(path, document)) {
    LOGW(L"Could not find anime season file.\nPath: {}", path);

    // Try to download from remote location
    if (!remote_location.empty()) {
      ui::ChangeStatusText(L"Downloading anime season data...");
      ui::DlgSeason.EnableInput(false);
      HttpRequest http_request;
      http_request.url = remote_location + filename;
      ConnectionManager.MakeRequest(http_request, taiga::kHttpSeasonsGet);
    }

    return false;
  }

  if (!LoadString(StrToWstr(document))) {
    ui::DisplayErrorMessage(L"Could not read anime season file.", path.c_str());
    return false;
  }

  return true;
}

bool SeasonDatabase::LoadString(const std::wstring& data) {
  xml_document document;
  xml_parse_result parse_result = document.load_string(data.c_str());

  if (parse_result.status != pugi::status_ok)
    return false;

  xml_node season_node = document.child(L"season");

  current_season = XmlReadStrValue(season_node.child(L"info"), L"name");
  time_t modified = ToTime(XmlReadStrValue(season_node.child(L"info"), L"modified"));

  items.clear();

  foreach_xmlnode_(node, season_node, L"anime") {
    std::map<enum_t, std::wstring> id_map;

    foreach_xmlnode_(id_node, node, L"id") {
      std::wstring id = id_node.child_value();
      std::wstring name = id_node.attribute(L"name").as_string();
      enum_t service_id = ServiceManager.GetServiceIdByName(name);
      id_map[service_id] = id;
    }

    int anime_id = anime::ID_UNKNOWN;
    anime::Item* anime_item = nullptr;

    for (const auto& pair : id_map) {
      anime_item = AnimeDatabase.FindItem(pair.second, pair.first, false);
      if (anime_item)
        break;
    }

    if (anime_item && anime_item->GetLastModified() >= modified) {
      anime_id = anime_item->GetId();
    } else {
      auto current_service_id = taiga::GetCurrentServiceId();
      if (id_map[current_service_id].empty()) {
        LOGD(L"{} - No ID for current service: {}", current_season.GetString(),
             XmlReadStrValue(node, L"title"));
        continue;
      }

      anime::Item item;
      item.SetSource(current_service_id);
      for (const auto& pair : id_map) {
        item.SetId(pair.second, pair.first);
      }
      item.SetLastModified(modified);
      item.SetTitle(XmlReadStrValue(node, L"title"));
      item.SetType(XmlReadIntValue(node, L"type"));
      item.SetImageUrl(XmlReadStrValue(node, L"image"));
      item.SetTrailerUrl(XmlReadStrValue(node, L"trailer"));
      item.SetProducers(XmlReadStrValue(node, L"producers"));
      anime_id = AnimeDatabase.UpdateItem(item);
    }

    items.push_back(anime_id);
  }

  if (!items.empty())
    AnimeDatabase.SaveDatabase();

  return true;
}

bool SeasonDatabase::LoadSeasonFromMemory(const anime::Season& season) {
  current_season = season;

  items.clear();
  Review();

  return true;
}

bool SeasonDatabase::IsRefreshRequired() {
  int count = 0;
  bool required = false;

  for (const auto& anime_id : items) {
    auto anime_item = AnimeDatabase.FindItem(anime_id);
    if (anime_item) {
      const Date& date_start = anime_item->GetDateStart();
      if (!anime::IsValidDate(date_start) || anime_item->GetSynopsis().empty())
        count++;
    }
    if (count > 20) {
      required = true;
      break;
    }
  }

  return required;
}

void SeasonDatabase::Reset() {
  items.clear();

  current_season.name = anime::Season::kUnknown;
  current_season.year = 0;
}

void SeasonDatabase::Review(bool hide_nsfw) {
  Date date_start, date_end;
  current_season.GetInterval(date_start, date_end);

  const auto is_within_date_interval =
      [&date_start, &date_end](const anime::Item& anime_item) {
        const Date& anime_start = anime_item.GetDateStart();
        if (anime_start.year() && anime_start.month())
          if (date_start <= anime_start && anime_start <= date_end)
            return true;
        return false;
      };

  const auto is_nsfw =
      [&hide_nsfw](const anime::Item& anime_item) {
        return hide_nsfw && IsNsfw(anime_item);
      };

  // Check for invalid items
  for (size_t i = 0; i < items.size(); i++) {
    const int anime_id = items.at(i);
    auto anime_item = AnimeDatabase.FindItem(anime_id);
    if (anime_item) {
      const Date& anime_start = anime_item->GetDateStart();
      if (is_nsfw(*anime_item) ||
          (anime::IsValidDate(anime_start) && !is_within_date_interval(*anime_item))) {
        items.erase(items.begin() + i--);
        LOGD(L"Removed item: #{} \"{}\" ({})", anime_id,
             anime_item->GetTitle(), anime_start.to_string());
      }
    }
  }

  // Check for missing items
  for (const auto& [anime_id, anime_item] : AnimeDatabase.items) {
    if (std::find(items.begin(), items.end(), anime_id) != items.end())
      continue;
    if (is_nsfw(anime_item) || !is_within_date_interval(anime_item))
      continue;
    items.push_back(anime_id);
    switch (taiga::GetCurrentServiceId()) {
      default:
        LOGD(L"Added item: #{} \"{}\" ({})", anime_id,
             anime_item.GetTitle(), anime_item.GetDateStart().to_string());
        break;
      case sync::kMyAnimeList:
        LOGD(L"\t<anime>\n"
             L"\t\t<type>" + ToWstr(anime_item.GetType()) + L"</type>\n"
             L"\t\t<id name=\"myanimelist\">" + ToWstr(anime_id) + L"</id>\n"
             L"\t\t<producers>" + Join(anime_item.GetProducers(), L", ") + L"</producers>\n"
             L"\t\t<image>" + anime_item.GetImageUrl() + L"</image>\n"
             L"\t\t<title>" + anime_item.GetTitle() + L"</title>\n"
             L"\t</anime>\n");
        break;
    }
  }
}

}  // namespace library