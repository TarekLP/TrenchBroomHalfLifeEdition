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

#include "BrushFace.h"

#include "Ensure.h"
#include "Polyhedron.h"
#include "mdl/MapFormat.h"
#include "mdl/Material.h"
#include "mdl/ParallelUVCoordSystem.h"
#include "mdl/ParaxialUVCoordSystem.h"
#include "mdl/TagMatcher.h"
#include "mdl/TagVisitor.h"
#include "mdl/Texture.h"
#include "mdl/UVCoordSystem.h"

#include "kdl/reflection_impl.h"
#include "kdl/result.h"

#include "vm/bbox.h"
#include "vm/intersection.h"
#include "vm/mat.h"
#include "vm/plane_io.h" // IWYU pragma: keep
#include "vm/scalar.h"
#include "vm/util.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <string>
#include <utility>

namespace tb::mdl
{
const BrushVertex* BrushFace::TransformHalfEdgeToVertex::operator()(
  const BrushHalfEdge* halfEdge) const
{
  return halfEdge->origin();
}

const BrushEdge* BrushFace::TransformHalfEdgeToEdge::operator()(
  const BrushHalfEdge* halfEdge) const
{
  return halfEdge->edge();
}

BrushFace::BrushFace(const BrushFace& other)
  : Taggable{other}
  , m_points{other.m_points}
  , m_boundary{other.m_boundary}
  , m_attributes{other.m_attributes}
  , m_materialReference{other.m_materialReference}
  , m_uvCoordSystem{other.m_uvCoordSystem ? other.m_uvCoordSystem->clone() : nullptr}
  , m_lineNumber{other.m_lineNumber}
  , m_lineCount{other.m_lineCount}
  , m_selected{other.m_selected}
{
}

BrushFace::BrushFace(BrushFace&& other) noexcept
  : Taggable{std::move(other)}
  /* We need to disable these warnings because the previous move is safe */
  // NOLINTBEGIN(bugprone-use-after-move)
  , m_points{std::move(other.m_points)}
  , m_boundary{std::move(other.m_boundary)}
  , m_attributes{std::move(other.m_attributes)}
  , m_materialReference{std::move(other.m_materialReference)}
  , m_uvCoordSystem{std::move(other.m_uvCoordSystem)}
  , m_geometry{other.m_geometry}
  , m_lineNumber{other.m_lineNumber}
  , m_lineCount{other.m_lineCount}
  , m_selected{other.m_selected}
// NOLINTEND(bugprone-use-after-move)
{
}

BrushFace& BrushFace::operator=(BrushFace other) noexcept
{
  using std::swap;
  swap(*this, other);
  return *this;
}

void swap(BrushFace& lhs, BrushFace& rhs) noexcept
{
  using std::swap;
  swap(static_cast<Taggable&>(lhs), static_cast<Taggable&>(rhs));
  swap(lhs.m_points, rhs.m_points);
  swap(lhs.m_boundary, rhs.m_boundary);
  swap(lhs.m_attributes, rhs.m_attributes);
  swap(lhs.m_materialReference, rhs.m_materialReference);
  swap(lhs.m_uvCoordSystem, rhs.m_uvCoordSystem);
  swap(lhs.m_geometry, rhs.m_geometry);
  swap(lhs.m_lineNumber, rhs.m_lineNumber);
  swap(lhs.m_lineCount, rhs.m_lineCount);
  swap(lhs.m_selected, rhs.m_selected);
  swap(lhs.m_markedToRenderFace, rhs.m_markedToRenderFace);
}

BrushFace::~BrushFace() = default;

kdl_reflect_impl(BrushFace);

Result<BrushFace> BrushFace::create(
  const vm::vec3d& point0,
  const vm::vec3d& point1,
  const vm::vec3d& point2,
  const BrushFaceAttributes& attributes,
  const MapFormat mapFormat)
{
  return mdl::isParallelUVCoordSystem(mapFormat)
           ? BrushFace::create(
               point0,
               point1,
               point2,
               attributes,
               std::make_unique<ParallelUVCoordSystem>(
                 point0, point1, point2, attributes))
           : BrushFace::create(
               point0,
               point1,
               point2,
               attributes,
               std::make_unique<ParaxialUVCoordSystem>(
                 point0, point1, point2, attributes));
}

Result<BrushFace> BrushFace::createFromStandard(
  const vm::vec3d& point0,
  const vm::vec3d& point1,
  const vm::vec3d& point2,
  const BrushFaceAttributes& inputAttribs,
  const MapFormat mapFormat)
{
  assert(mapFormat != MapFormat::Unknown);

  auto uvCoordSystem = std::unique_ptr<UVCoordSystem>{};
  auto attribs = BrushFaceAttributes{""};

  if (mdl::isParallelUVCoordSystem(mapFormat))
  {
    // Convert paraxial to parallel
    std::tie(uvCoordSystem, attribs) =
      ParallelUVCoordSystem::fromParaxial(point0, point1, point2, inputAttribs);
  }
  else
  {
    // Pass through paraxial
    uvCoordSystem =
      std::make_unique<ParaxialUVCoordSystem>(point0, point1, point2, inputAttribs);
    attribs = inputAttribs;
  }

  return BrushFace::create(point0, point1, point2, attribs, std::move(uvCoordSystem));
}

Result<BrushFace> BrushFace::createFromValve(
  const vm::vec3d& point1,
  const vm::vec3d& point2,
  const vm::vec3d& point3,
  const BrushFaceAttributes& inputAttribs,
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  MapFormat mapFormat)
{
  assert(mapFormat != MapFormat::Unknown);

  auto uvCoordSystem = std::unique_ptr<UVCoordSystem>{};
  auto attribs = BrushFaceAttributes{""};

  if (mdl::isParallelUVCoordSystem(mapFormat))
  {
    // Pass through parallel
    uvCoordSystem = std::make_unique<ParallelUVCoordSystem>(uAxis, vAxis);
    attribs = inputAttribs;
  }
  else
  {
    // Convert parallel to paraxial
    std::tie(uvCoordSystem, attribs) = ParaxialUVCoordSystem::fromParallel(
      point1, point2, point3, inputAttribs, uAxis, vAxis);
  }

  return BrushFace::create(point1, point2, point3, attribs, std::move(uvCoordSystem));
}

Result<BrushFace> BrushFace::create(
  const vm::vec3d& point0,
  const vm::vec3d& point1,
  const vm::vec3d& point2,
  const BrushFaceAttributes& attributes,
  std::unique_ptr<UVCoordSystem> uvCoordSystem)
{
  auto points = Points{{vm::correct(point0), vm::correct(point1), vm::correct(point2)}};
  if (const auto plane = vm::from_points(points[0], points[1], points[2]))
  {
    return BrushFace{points, *plane, attributes, std::move(uvCoordSystem)};
  }
  return Error{"Brush has invalid face"};
}

BrushFace::BrushFace(
  const BrushFace::Points& points,
  const vm::plane3d& boundary,
  BrushFaceAttributes attributes,
  std::unique_ptr<UVCoordSystem> uvCoordSystem)
  : m_points{points}
  , m_boundary{boundary}
  , m_attributes{std::move(attributes)}
  , m_uvCoordSystem{std::move(uvCoordSystem)}
{
  ensure(m_uvCoordSystem != nullptr, "uvCoordSystem is null");
}

void BrushFace::sortFaces(std::vector<BrushFace>& faces)
{
  // Originally, the idea to sort faces came from TxQBSP, but the sorting used there was
  // not entirely clear to me. But it is still desirable to have a deterministic order
  // in which the faces are added to the brush, so I chose to just sort the faces by
  // their normals.

  std::sort(std::begin(faces), std::end(faces), [](const auto& lhs, const auto& rhs) {
    const auto& lhsBoundary = lhs.boundary();
    const auto& rhsBoundary = rhs.boundary();

    const auto cmp = vm::compare(lhsBoundary.normal, rhsBoundary.normal);
    return cmp < 0 ? true : cmp > 0 ? false : lhsBoundary.distance < rhsBoundary.distance;
  });
}

std::unique_ptr<UVCoordSystemSnapshot> BrushFace::takeUVCoordSystemSnapshot() const
{
  return m_uvCoordSystem->takeSnapshot();
}

void BrushFace::restoreUVCoordSystemSnapshot(
  const UVCoordSystemSnapshot& coordSystemSnapshot)
{
  coordSystemSnapshot.restore(*m_uvCoordSystem);
}

void BrushFace::copyUVCoordSystemFromFace(
  const UVCoordSystemSnapshot& coordSystemSnapshot,
  const BrushFaceAttributes& attributes,
  const vm::plane3d& sourceFacePlane,
  const WrapStyle wrapStyle)
{
  // Get a line, and a reference point, that are on both the source face's plane and our
  // plane
  const auto seam =
    vm::intersect_plane_plane(sourceFacePlane, m_boundary).value_or(vm::line3d{});
  const auto refPoint = vm::project_point(seam, center());

  coordSystemSnapshot.restore(*m_uvCoordSystem);

  // Get the UV coords at the refPoint using the source face's attributes and tex coord
  // system
  const auto desriedCoords =
    m_uvCoordSystem->uvCoords(refPoint, attributes, vm::vec2f{1, 1});

  m_uvCoordSystem->setNormal(
    sourceFacePlane.normal, m_boundary.normal, m_attributes, wrapStyle);

  // Adjust the offset on this face so that the UV coordinates at the refPoint stay
  // the same
  if (!vm::is_zero(seam.direction, vm::Cd::almost_zero()))
  {
    const auto currentCoords =
      m_uvCoordSystem->uvCoords(refPoint, m_attributes, vm::vec2f::one());
    const auto offsetChange = desriedCoords - currentCoords;
    m_attributes.setOffset(correct(modOffset(m_attributes.offset() + offsetChange), 4));
  }
}

const BrushFace::Points& BrushFace::points() const
{
  return m_points;
}

const vm::plane3d& BrushFace::boundary() const
{
  return m_boundary;
}

const vm::vec3d& BrushFace::normal() const
{
  return boundary().normal;
}

vm::vec3d BrushFace::center() const
{
  ensure(m_geometry != nullptr, "geometry is null");
  const auto& boundary = m_geometry->boundary();
  return vm::average(
    std::begin(boundary), std::end(boundary), BrushGeometry::GetVertexPosition());
}

vm::vec3d BrushFace::boundsCenter() const
{
  ensure(m_geometry != nullptr, "geometry is null");

  const auto toPlane =
    vm::plane_projection_matrix(m_boundary.distance, m_boundary.normal);
  const auto fromPlane = vm::invert(toPlane);

  const auto* first = m_geometry->boundary().front();
  const auto* current = first;

  auto bounds = vm::bbox3d{};
  bounds.min = bounds.max = toPlane * current->origin()->position();

  current = current->next();
  while (current != first)
  {
    bounds = merge(bounds, toPlane * current->origin()->position());
    current = current->next();
  }
  return *fromPlane * bounds.center();
}

double BrushFace::projectedArea(const vm::axis::type axis) const
{
  auto c1 = 0.0;
  auto c2 = 0.0;
  for (const BrushHalfEdge* halfEdge : m_geometry->boundary())
  {
    const auto origin = vm::swizzle(halfEdge->origin()->position(), axis);
    const auto destination = vm::swizzle(halfEdge->destination()->position(), axis);
    c1 += origin.x() * destination.y();
    c2 += origin.y() * destination.x();
  }
  return vm::abs((c1 - c2) / 2.0);
}

double BrushFace::area() const
{
  auto result = 0.0;

  const auto* firstEdge = m_geometry->boundary().front();
  const auto* currentEdge = firstEdge->next();
  const auto* vertex0 = firstEdge->origin();
  do
  {
    const auto* vertex1 = currentEdge->origin();
    const auto* vertex2 = currentEdge->destination();
    const auto side0 = vertex1->position() - vertex0->position();
    const auto side1 = vertex2->position() - vertex0->position();
    result += vm::length(vm::cross(side0, side1));

    currentEdge = currentEdge->next();
  } while (currentEdge->next() != firstEdge);

  return result / 2.0;
}

bool BrushFace::coplanarWith(const vm::plane3d& plane) const
{
  // Test if the face's center lies on the reference plane within an epsilon.
  if (!vm::is_zero(
        plane.point_distance(center()), vm::constants<double>::almost_zero() * 10.0))
  {
    return false;
  }

  // Test if the normals are colinear by checking their enclosed angle.
  if (
    1.0 - vm::dot(boundary().normal, plane.normal)
    >= vm::constants<double>::colinear_epsilon())
  {
    return false;
  }
  return true;
}

const BrushFaceAttributes& BrushFace::attributes() const
{
  return m_attributes;
}

void BrushFace::setAttributes(const BrushFaceAttributes& attributes)
{
  const auto oldRotation = m_attributes.rotation();
  m_attributes = attributes;
  m_uvCoordSystem->setRotation(m_boundary.normal, oldRotation, m_attributes.rotation());
}

bool BrushFace::setAttributes(const BrushFace& other)
{
  auto result = false;
  result |= m_attributes.setMaterialName(other.attributes().materialName());
  result |= m_attributes.setXOffset(other.attributes().xOffset());
  result |= m_attributes.setYOffset(other.attributes().yOffset());
  result |= m_attributes.setRotation(other.attributes().rotation());
  result |= m_attributes.setXScale(other.attributes().xScale());
  result |= m_attributes.setYScale(other.attributes().yScale());
  result |= m_attributes.setSurfaceContents(other.attributes().surfaceContents());
  result |= m_attributes.setSurfaceFlags(other.attributes().surfaceFlags());
  result |= m_attributes.setSurfaceValue(other.attributes().surfaceValue());
  return result;
}

namespace
{

struct SurfaceData
{
  int surfaceContents;
  int surfaceFlags;
  float surfaceValue;
};

SurfaceData getDefaultSurfaceData(const Material* material)
{
  if (const auto* texture = getTexture(material))
  {
    const auto& defaults = texture->embeddedDefaults();
    if (const auto* q2Defaults = std::get_if<Q2EmbeddedDefaults>(&defaults))
    {
      return {
        q2Defaults->contents,
        q2Defaults->flags,
        float(q2Defaults->value),
      };
    }
  }
  return {0, 0, 0.0f};
}

SurfaceData resolveSurfaceData(
  const BrushFaceAttributes& attributes, const Material* material)
{
  const auto defaultSurfaceData = getDefaultSurfaceData(material);
  return {
    attributes.surfaceContents().value_or(defaultSurfaceData.surfaceContents),
    attributes.surfaceFlags().value_or(defaultSurfaceData.surfaceFlags),
    attributes.surfaceValue().value_or(defaultSurfaceData.surfaceValue)};
}

} // namespace

int BrushFace::resolvedSurfaceContents() const
{
  return resolveSurfaceData(m_attributes, material()).surfaceContents;
}

int BrushFace::resolvedSurfaceFlags() const
{
  return resolveSurfaceData(m_attributes, material()).surfaceFlags;
}

float BrushFace::resolvedSurfaceValue() const
{
  return resolveSurfaceData(m_attributes, material()).surfaceValue;
}

Color BrushFace::resolvedColor() const
{
  return m_attributes.color().value_or(Color{});
}

void BrushFace::resetUVCoordSystemCache()
{
  if (m_uvCoordSystem)
  {
    m_uvCoordSystem->resetCache(m_points[0], m_points[1], m_points[2], m_attributes);
  }
}

const UVCoordSystem& BrushFace::uvCoordSystem() const
{
  return *m_uvCoordSystem;
}

const Material* BrushFace::material() const
{
  return m_materialReference.get();
}

vm::vec2f BrushFace::textureSize() const
{
  if (const auto* texture = getTexture(material()))
  {
    return vm::max(texture->sizef(), vm::vec2f{1, 1});
  }
  return vm::vec2f{1, 1};
}

vm::vec2f BrushFace::modOffset(const vm::vec2f& offset) const
{
  return m_attributes.modOffset(offset, textureSize());
}

bool BrushFace::setMaterial(Material* material)
{
  if (material == this->material())
  {
    return false;
  }

  m_materialReference = AssetReference(material);
  return true;
}

vm::vec3d BrushFace::uAxis() const
{
  return m_uvCoordSystem->uAxis();
}

vm::vec3d BrushFace::vAxis() const
{
  return m_uvCoordSystem->vAxis();
}

void BrushFace::resetUVAxes()
{
  m_uvCoordSystem->reset(m_boundary.normal);
}

void BrushFace::resetUVAxesToParaxial()
{
  m_uvCoordSystem->resetToParaxial(m_boundary.normal, 0.0f);
}

void BrushFace::convertToParaxial()
{
  auto [newUVCoordSystem, newAttributes] =
    m_uvCoordSystem->toParaxial(m_points[0], m_points[1], m_points[2], m_attributes);

  m_attributes = newAttributes;
  m_uvCoordSystem = std::move(newUVCoordSystem);
}

void BrushFace::convertToParallel()
{
  auto [newUVCoordSystem, newAttributes] =
    m_uvCoordSystem->toParallel(m_points[0], m_points[1], m_points[2], m_attributes);

  m_attributes = newAttributes;
  m_uvCoordSystem = std::move(newUVCoordSystem);
}

void BrushFace::moveUV(
  const vm::vec3d& up, const vm::vec3d& right, const vm::vec2f& offset)
{
  m_uvCoordSystem->translate(m_boundary.normal, up, right, offset, m_attributes);
}

void BrushFace::rotateUV(const float angle)
{
  const auto oldRotation = m_attributes.rotation();
  m_uvCoordSystem->rotate(m_boundary.normal, angle, m_attributes);
  m_uvCoordSystem->setRotation(m_boundary.normal, oldRotation, m_attributes.rotation());
}

void BrushFace::shearUV(const vm::vec2f& factors)
{
  m_uvCoordSystem->shear(m_boundary.normal, factors);
}

void BrushFace::flipUV(
  const vm::vec3d& /* cameraUp */,
  const vm::vec3d& cameraRight,
  const vm::direction cameraRelativeFlipDirection)
{
  const auto texToWorld = m_uvCoordSystem->fromMatrix(vm::vec2f{0, 0}, vm::vec2f{1, 1});

  const auto texUAxisInWorld = vm::normalize((texToWorld * vm::vec4d(1, 0, 0, 0)).xyz());
  const auto texVAxisInWorld = vm::normalize((texToWorld * vm::vec4d(0, 1, 0, 0)).xyz());

  // Get the cos(angle) between cameraRight and the texUAxisInWorld _line_ (so, take the
  // smaller of the angles among -texUAxisInWorld and texUAxisInWorld). Note that larger
  // cos(angle) means smaller angle.
  const auto uAxisCosAngle = vm::max(
    vm::dot(texUAxisInWorld, cameraRight), vm::dot(-texUAxisInWorld, cameraRight));

  const auto vAxisCosAngle = vm::max(
    vm::dot(texVAxisInWorld, cameraRight), vm::dot(-texVAxisInWorld, cameraRight));

  // If this is true, it means the V axis is closer to the camera's right vector than
  // the U axis is (i.e. we're looking at the material sideways), so we should map
  // "camera relative horizontal" to "material space Y".
  const auto cameraRightCloserToV = (vAxisCosAngle > uAxisCosAngle);

  auto flipUAxis =
    (cameraRelativeFlipDirection == vm::direction::left
     || cameraRelativeFlipDirection == vm::direction::right);

  if (cameraRightCloserToV)
  {
    flipUAxis = !flipUAxis;
  }

  if (flipUAxis)
  {
    m_attributes.setXScale(-1.0f * m_attributes.xScale());
  }
  else
  {
    m_attributes.setYScale(-1.0f * m_attributes.yScale());
  }
}

Result<void> BrushFace::transform(const vm::mat4x4d& transform, const bool lockAlignment)
{
  using std::swap;

  const auto invariant = m_geometry ? center() : m_boundary.anchor();
  const auto oldBoundary = m_boundary;

  m_boundary = m_boundary.transform(transform);
  for (size_t i = 0; i < 3; ++i)
  {
    m_points[i] = transform * m_points[i];
  }

  if (
    vm::dot(
      vm::cross(m_points[2] - m_points[0], m_points[1] - m_points[0]), m_boundary.normal)
    < 0.0)
  {
    swap(m_points[1], m_points[2]);
  }

  return setPoints(m_points[0], m_points[1], m_points[2]) | kdl::transform([&]() {
           m_uvCoordSystem->transform(
             oldBoundary,
             m_boundary,
             transform,
             m_attributes,
             textureSize(),
             lockAlignment,
             invariant);
         });
}

void BrushFace::invert()
{
  using std::swap;

  m_boundary = m_boundary.flip();
  swap(m_points[1], m_points[2]);
}

Result<void> BrushFace::updatePointsFromVertices()
{
  ensure(m_geometry != nullptr, "geometry is null");

  const auto* first = m_geometry->boundary().front();
  const auto oldPlane = m_boundary;
  return setPoints(
           first->next()->origin()->position(),
           first->origin()->position(),
           first->previous()->origin()->position())
         | kdl::transform([&]() {
             // Get a line, and a reference point, that are on both the old plane
             // (before moving the face) and after moving the face.
             if (const auto seam = vm::intersect_plane_plane(oldPlane, m_boundary))
             {
               const auto refPoint = project_point(*seam, center());

               // Get the UV coordinates at the refPoint using the old face's attribs
               // and UV coordinage system
               const auto desriedCoords =
                 m_uvCoordSystem->uvCoords(refPoint, m_attributes, vm::vec2f{1, 1});

               m_uvCoordSystem->setNormal(
                 oldPlane.normal, m_boundary.normal, m_attributes, WrapStyle::Projection);

               // Adjust the offset on this face so that the UV coordinates at the
               // refPoint stay the same
               const auto currentCoords =
                 m_uvCoordSystem->uvCoords(refPoint, m_attributes, vm::vec2f{1, 1});
               const auto offsetChange = desriedCoords - currentCoords;
               m_attributes.setOffset(
                 correct(modOffset(m_attributes.offset() + offsetChange), 4));
             }
           });
}

vm::mat4x4d BrushFace::projectToBoundaryMatrix() const
{
  const auto texZAxis =
    m_uvCoordSystem->fromMatrix(vm::vec2f{0, 0}, vm::vec2f{1, 1}) * vm::vec3d{0, 0, 1};
  const auto worldToPlaneMatrix =
    vm::plane_projection_matrix(m_boundary.distance, m_boundary.normal, texZAxis);
  const auto planeToWorldMatrix = vm::invert(worldToPlaneMatrix);
  return *planeToWorldMatrix * vm::mat4x4d::zero_out<2>() * worldToPlaneMatrix;
}

vm::mat4x4d BrushFace::toUVCoordSystemMatrix(
  const vm::vec2f& offset, const vm::vec2f& scale, const bool project) const
{
  return project ? vm::mat4x4d::zero_out<2>() * m_uvCoordSystem->toMatrix(offset, scale)
                 : m_uvCoordSystem->toMatrix(offset, scale);
}

vm::mat4x4d BrushFace::fromUVCoordSystemMatrix(
  const vm::vec2f& offset, const vm::vec2f& scale, const bool project) const
{
  return project ? projectToBoundaryMatrix() * m_uvCoordSystem->fromMatrix(offset, scale)
                 : m_uvCoordSystem->fromMatrix(offset, scale);
}

float BrushFace::measureUVAngle(const vm::vec2f& center, const vm::vec2f& point) const
{
  return m_uvCoordSystem->measureAngle(m_attributes.rotation(), center, point);
}

size_t BrushFace::vertexCount() const
{
  assert(m_geometry != nullptr);
  return m_geometry->boundary().size();
}

std::vector<vm::vec3d> BrushFace::vertexPositions() const
{
  ensure(m_geometry != nullptr, "geometry is null");
  return m_geometry->vertexPositions();
}

bool BrushFace::hasVertices(const vm::polygon3d& vertices, const double epsilon) const
{
  ensure(m_geometry != nullptr, "geometry is null");
  return m_geometry->hasVertexPositions(vertices.vertices(), epsilon);
}

vm::polygon3d BrushFace::polygon() const
{
  ensure(m_geometry != nullptr, "geometry is null");
  return vm::polygon3d(vertexPositions());
}

BrushFaceGeometry* BrushFace::geometry() const
{
  return m_geometry;
}

void BrushFace::setGeometry(BrushFaceGeometry* geometry)
{
  m_geometry = geometry;
}

size_t BrushFace::lineNumber() const
{
  return m_lineNumber;
}

void BrushFace::setFilePosition(const size_t lineNumber, const size_t lineCount) const
{
  m_lineNumber = lineNumber;
  m_lineCount = lineCount;
}

bool BrushFace::selected() const
{
  return m_selected;
}

void BrushFace::select()
{
  assert(!m_selected);
  m_selected = true;
}

void BrushFace::deselect()
{
  assert(m_selected);
  m_selected = false;
}

vm::vec2f BrushFace::uvCoords(const vm::vec3d& point) const
{
  return m_uvCoordSystem->uvCoords(point, m_attributes, textureSize());
}

std::optional<double> BrushFace::intersectWithRay(const vm::ray3d& ray) const
{
  ensure(m_geometry != nullptr, "geometry is null");

  const auto cos = vm::dot(m_boundary.normal, ray.direction);
  return cos < 0.0 ? vm::intersect_ray_polygon(
                       ray,
                       m_boundary,
                       m_geometry->boundary().begin(),
                       m_geometry->boundary().end(),
                       BrushGeometry::GetVertexPosition{})
                   : std::nullopt;
}

Result<void> BrushFace::setPoints(
  const vm::vec3d& point0, const vm::vec3d& point1, const vm::vec3d& point2)
{
  m_points[0] = point0;
  m_points[1] = point1;
  m_points[2] = point2;
  correctPoints();

  if (const auto plane = vm::from_points(m_points[0], m_points[1], m_points[2]))
  {
    m_boundary = *plane;
    return kdl::void_success;
  }
  return Error{"Brush has invalid face"};
}

void BrushFace::correctPoints()
{
  for (size_t i = 0; i < 3; ++i)
  {
    m_points[i] = correct(m_points[i]);
  }
}

void BrushFace::setMarked(const bool marked) const
{
  m_markedToRenderFace = marked;
}

bool BrushFace::isMarked() const
{
  return m_markedToRenderFace;
}

void BrushFace::doAcceptTagVisitor(TagVisitor& visitor)
{
  visitor.visit(*this);
}

void BrushFace::doAcceptTagVisitor(ConstTagVisitor& visitor) const
{
  visitor.visit(*this);
}

} // namespace tb::mdl
