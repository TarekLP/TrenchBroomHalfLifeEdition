
/*
 Copyright (C) 2025 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "TestUtils.h"

#include <QFile>
#include <QTextStream>

namespace upd
{

std::optional<QString> readFileIntoString(QFile& file)
{
  if (file.open(QIODevice::ReadOnly))
  {
    auto in = QTextStream{&file};
    return in.readAll();
  }
  return std::nullopt;
}

std::optional<QString> readFileIntoString(const QString& path)
{
  auto file = QFile{path};
  return readFileIntoString(file);
}

} // namespace upd
