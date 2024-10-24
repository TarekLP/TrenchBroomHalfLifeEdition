/*
 Copyright (C) 2010 Kristian Duske

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

#pragma once

#include "ui/MultiPaneMapView.h"

#include <memory>

class QSplitter;

namespace tb::render
{
class MapRenderer;
class VboManager;
} // namespace tb::render

namespace tb::ui
{
class CyclingMapView;
class GLContextManager;
class MapDocument;
class MapViewBase;
class MapView2D;
class MapView3D;
class MapViewToolBox;

class ThreePaneMapView : public MultiPaneMapView
{
  Q_OBJECT
private:
  std::weak_ptr<MapDocument> m_document;

  QSplitter* m_hSplitter = nullptr;
  QSplitter* m_vSplitter = nullptr;
  MapView3D* m_mapView3D = nullptr;
  MapView2D* m_mapViewXY = nullptr;
  CyclingMapView* m_mapViewZZ = nullptr;

public:
  ThreePaneMapView(
    std::weak_ptr<MapDocument> document,
    MapViewToolBox& toolBox,
    render::MapRenderer& mapRenderer,
    GLContextManager& contextManager,
    QWidget* parent = nullptr);
  ~ThreePaneMapView() override;

private:
  void createGui(
    MapViewToolBox& toolBox,
    render::MapRenderer& mapRenderer,
    GLContextManager& contextManager);

private: // implement MultiPaneMapView subclassing interface
  void maximizeView(MapView* view) override;
  void restoreViews() override;
};

} // namespace tb::ui