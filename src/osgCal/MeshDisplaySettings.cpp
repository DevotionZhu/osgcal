/* -*- c++ -*-
    Copyright (C) 2007 Vladimir Shabanov <vshabanoff@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <osgCal/MeshDisplaySettings>

using namespace osgCal;

MeshDisplaySettings::MeshDisplaySettings()
    : software( false )
    , showTBN( false )
    , fogMode( (osg::Fog::Mode)0 )
    , useDepthFirstMesh( false )
{
}

static osg::ref_ptr< MeshDisplaySettings > defaultMeshDisplaySettings( new MeshDisplaySettings );

const MeshDisplaySettings*
MeshDisplaySettings::defaults()
{
    return defaultMeshDisplaySettings.get();
}

MeshDisplaySettings* 
DefaultMeshDisplaySettingsSelector::getDisplaySettings( const MeshData* )
{
    return defaultMeshDisplaySettings.get();
}

static osg::ref_ptr< DefaultMeshDisplaySettingsSelector >
defaultMeshDisplaySettingsSelector( new DefaultMeshDisplaySettingsSelector );

DefaultMeshDisplaySettingsSelector*
DefaultMeshDisplaySettingsSelector::instance()
{
    return defaultMeshDisplaySettingsSelector.get();
}
