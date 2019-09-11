/*
** Taiga
** Copyright (C) 2010-2019, Eren Okka
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

#include "xml.h"

#include "file.h"
#include "string.h"

XmlAttribute XmlAttr(XmlNode& node, const std::wstring_view name) {
  auto attr = node.attribute(name.data());
  if (!attr) {
    attr = node.append_attribute(name.data());
  }
  return attr;
}

XmlNode XmlChild(XmlNode& node, const std::wstring_view name) {
  auto child = node.child(name.data());
  if (!child) {
    child = node.append_child(name.data());
  }
  return child;
}

////////////////////////////////////////////////////////////////////////////////

std::wstring XmlDump(const XmlNode node) {
  struct xml_string_writer : pugi::xml_writer {
    std::wstring result;
    void write(const void* data, size_t size) override {
      result.append(static_cast<const pugi::char_t*>(data), size);
    }
  };

  xml_string_writer writer;
  node.print(writer);

  return std::move(writer.result);
}

////////////////////////////////////////////////////////////////////////////////

int XmlReadInt(const XmlNode& node, const std::wstring_view name) {
  return ToInt(node.child_value(name.data()));
}

std::wstring XmlReadStr(const XmlNode& node, const std::wstring_view name) {
  return node.child_value(name.data());
}

////////////////////////////////////////////////////////////////////////////////

void XmlWriteInt(XmlNode& node, const std::wstring_view name,
                 const int value) {
  node.append_child(name.data()).text().set(ToWstr(value).c_str());
}

void XmlWriteStr(XmlNode& node, const std::wstring_view name,
                 const std::wstring_view value, pugi::xml_node_type node_type) {
  node.append_child(name.data())
      .append_child(node_type).set_value(value.data());
}

////////////////////////////////////////////////////////////////////////////////

void XmlWriteChildNodes(XmlNode& parent_node,
                        const std::vector<std::wstring>& input,
                        const std::wstring_view name,
                        pugi::xml_node_type node_type) {
  for (const auto& value : input) {
    XmlNode child_node = parent_node.append_child(name.data());
    child_node.append_child(node_type).set_value(value.c_str());
  }
}

bool XmlWriteDocumentToFile(const XmlDocument& document,
                            const std::wstring_view path) {
  CreateFolder(GetPathOnly(std::wstring{path}));

  constexpr auto indent = L"\x09";  // horizontal tab
  constexpr auto flags = pugi::format_default | pugi::format_write_bom;
  return document.save_file(path.data(), indent, flags);
}
