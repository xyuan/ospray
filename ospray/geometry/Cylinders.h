// ======================================================================== //
// Copyright 2009-2019 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "Geometry.h"

namespace ospray {

  struct OSPRAY_SDK_INTERFACE Cylinders : public Geometry
  {
    Cylinders();
    //! \brief common function to help printf-debugging
    virtual std::string toString() const override;
    /*! \brief integrates this geometry's primitives into the respective
        model's acceleration structure */
    virtual void finalize(World *model) override;

    float radius;  //!< default radius, if no per-cylinder radius was specified.
    int32 materialID;

    size_t numCylinders;
    size_t bytesPerCylinder;  //!< num bytes per cylinder
    int64 offset_v0;
    int64 offset_v1;
    int64 offset_radius;
    int64 offset_materialID;
    int64 offset_colorID;

    Ref<Data> cylinderData;
    Ref<Data> colorData; /*!< cylinder color array */
    Ref<Data> texcoordData;
  };
  /*! @} */

}  // namespace ospray
