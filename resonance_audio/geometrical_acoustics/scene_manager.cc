/*
Copyright 2018 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "geometrical_acoustics/scene_manager.h"

#include "base/aligned_allocator.h"
#include "geometrical_acoustics/sphere.h"

namespace vraudio {

namespace {

// A function adapter from SphereBounds() to RTCBoundsFunc in order to be
// passed to rtcSetBoundsFunction().
// The signature of RTCBoundsFunc does not comply with Google's C++ style,

static void EmbreeSphereBoundsFunction(const RTCBoundsFunctionArguments* args)
 {
   void* user_data = args->geometryUserPtr;
   size_t index = args->primID;
   // unsigned int time = args->timeStep;
   RTCBounds& output_bounds = *args->bounds_o;
  Sphere* spheres = static_cast<Sphere*>(user_data);
  const Sphere& sphere = spheres[index];
  SphereBounds(sphere, &output_bounds);
}

// A function adapter from SphereIntersections() to RTCIntersectFunc in order
// to be passed to rtcSetIntersectFunction().
// The signature of RTCIntersectFunc does not comply with Google's C++ style,

static void EmbreeSphereIntersectFunction(const RTCIntersectFunctionNArguments* args) // EMBREE_FIXME: may also be RTCOccludedFunctionNArguments
 {
   void* valid_in = args->valid;
   int* vvalid_in = (int* ) valid_in;
   void* user_data = args->geometryUserPtr;
   // RTCIntersectContext* context_in = args->context;
   RTCRayHit* ray_ptr_in = (RTCRayHit* ) args->rayhit;
   RTCRayHit& ray = *ray_ptr_in;
   unsigned int N_in = args->N;
   unsigned int index = args->primID;
   assert(N_in == 1);
   if (*vvalid_in != -1) return;
   // EMBREE_FIXME: intersect func: on hit copy context_in->instID[0] to ray_in.hit.instID[0]
   // EMBREE_FIXME: occluded func: on hit set ray_in.tfar = -inf
  Sphere* const spheres = static_cast<Sphere*>(user_data);
  const Sphere& sphere = spheres[index];
  SphereIntersection(sphere, &ray);
}

}  // namespace

SceneManager::SceneManager() {
  // Use a single RTCDevice for all scenes.
  device_ = rtcNewDevice(nullptr);
  CHECK_NOTNULL(device_);
  scene_ = rtcNewScene(device_);
  // rtcSetSceneFlags(scene_,RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_HIGH); // EMBREE_FIXME: set proper scene flags
  // rtcSetSceneBuildQuality(scene_,RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_HIGH); // EMBREE_FIXME: set proper build quality
  listener_scene_ = rtcNewScene(device_);
  // rtcSetSceneFlags(listener_scene_,RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_HIGH); // EMBREE_FIXME: set proper scene flags
  // rtcSetSceneBuildQuality(listener_scene_,RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_HIGH); // EMBREE_FIXME: set proper build quality
}

SceneManager::~SceneManager() {
  rtcReleaseScene(scene_);
  rtcReleaseScene(listener_scene_);
  rtcReleaseDevice (device_);
}

void SceneManager::BuildScene(const std::vector<Vertex>& vertex_buffer,
                              const std::vector<Triangle>& triangle_buffer) {
  num_vertices_ = vertex_buffer.size();
  num_triangles_ = triangle_buffer.size();
  unsigned int mesh_id;
  RTCGeometry geom_0 = rtcNewGeometry (g_device, RTC_GEOMETRY_TYPE_TRIANGLE); // EMBREE_FIXME: check if geometry gets properly committed
  rtcSetGeometryBuildQuality(geom_0,RTC_BUILD_QUALITY_MEDIUM);
  rtcSetGeometryTimeStepCount(geom_0,1);
  mesh_id = rtcAttachGeometry(scene_,geom_0);
  rtcReleaseGeometry(geom_0);
  struct EmbreeVertex {
    // Embree uses 4 floats for each vertex for alignment. The last value
    // is for padding only.
    float x, y, z, a;
  };
  EmbreeVertex* const embree_vertex_array = static_cast<EmbreeVertex*>(
      rtcSetNewGeometryBuffer(geom_0,RTC_BUFFER_TYPE_VERTEX,0,RTC_FORMAT_FLOAT3,4*sizeof(float),num_vertices_));
  for (size_t i = 0; i < num_vertices_; ++i) {
    embree_vertex_array[i].x = vertex_buffer[i].x;
    embree_vertex_array[i].y = vertex_buffer[i].y;
    embree_vertex_array[i].z = vertex_buffer[i].z;
  }
  

  // Triangles. Somehow Embree is a left-handed system, so we re-order all
  // triangle indices here, i.e. {v0, v1, v2} -> {v0, v2, v1}.
  int* const embree_index_array =
      static_cast<int*>(rtcSetNewGeometryBuffer(geom_0,RTC_BUFFER_TYPE_INDEX,0,RTC_FORMAT_UINT3,3*sizeof(int),num_triangles_));
  for (size_t i = 0; i < num_triangles_; ++i) {
    embree_index_array[3 * i + 0] = triangle_buffer[i].v0;
    embree_index_array[3 * i + 1] = triangle_buffer[i].v2;
    embree_index_array[3 * i + 2] = triangle_buffer[i].v1;
  }
  
  rtcCommitScene(scene_);
  is_scene_committed_ = true;
}

bool SceneManager::AssociateReflectionKernelToTriangles(
    const ReflectionKernel& reflection,
    const std::unordered_set<unsigned int>& triangle_indices) {
  const size_t reflection_index = reflections_.size();
  reflections_.push_back(reflection);
  for (const unsigned int triangle_index : triangle_indices) {
    if (triangle_index >= num_triangles_) {
      return false;
    }
    triangle_to_reflection_map_[triangle_index] = reflection_index;
  }
  return true;
}

void SceneManager::BuildListenerScene(
    const std::vector<AcousticListener>& listeners,
    float listener_sphere_radius) {
  rtcReleaseScene(listener_scene_);
  listener_scene_ = rtcNewScene(device_);
  // rtcSetSceneFlags(listener_scene_,RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_HIGH); // EMBREE_FIXME: set proper scene flags
  // rtcSetSceneBuildQuality(listener_scene_,RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_HIGH); // EMBREE_FIXME: set proper build quality

  for (size_t listener_index = 0; listener_index < listeners.size();
       ++listener_index) {
    // Create a sphere per listener and add to |listener_scene_|.
    const AcousticListener& listener = listeners.at(listener_index);
    const unsigned int sphere_id;
     RTCGeometry geom_1 = rtcNewGeometry (g_device, RTC_GEOMETRY_TYPE_USER); // EMBREE_FIXME: check if geometry gets properly committed
     rtcSetGeometryBuildQuality(geom_1,RTC_BUILD_QUALITY_MEDIUM);
     rtcSetGeometryUserPrimitiveCount(geom_1,1);
     rtcSetGeometryTimeStepCount(geom_1,1);
     sphere_id = rtcAttachGeometry(listener_scene_,geom_1);
     rtcReleaseGeometry(geom_1);;
    Sphere* const sphere =
        AllignedMalloc<Sphere, size_t, Sphere*>(sizeof(Sphere),
                                                /*alignment=*/64);
    sphere->center[0] = listener.position[0];
    sphere->center[1] = listener.position[1];
    sphere->center[2] = listener.position[2];
    sphere->radius = listener_sphere_radius;
    sphere->geometry_id = sphere_id;

    // rtcSetUserData() takes ownership of |sphere|.
    rtcSetGeometryUserData(geom_1,sphere);
    rtcSetGeometryBoundsFunction(geom_1,&EmbreeSphereBoundsFunction,NULL);
    rtcSetGeometryIntersectFunction (geom_1, &EmbreeSphereIntersectFunction);

    // Associate the listener to |sphere_id| through its index in the vector
    // of listeners.
    sphere_to_listener_map_[sphere_id] = listener_index;
  }

  rtcCommitScene(listener_scene_);
  is_listener_scene_committed_ = true;
}

}  // namespace vraudio
