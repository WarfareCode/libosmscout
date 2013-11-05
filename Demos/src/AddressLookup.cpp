/*
  AddressLookup - a demo program for libosmscout
  Copyright (C) 2010  Tim Teulings

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <iostream>
#include <iomanip>

#include <osmscout/Database.h>

const static size_t RESULT_SET_MAX_SIZE = 1000;

std::string GetName(const osmscout::ObjectFileRef& object,
                    OSMSCOUT_HASHMAP<osmscout::FileOffset, osmscout::NodeRef> nodesMap,
                    OSMSCOUT_HASHMAP<osmscout::FileOffset, osmscout::AreaRef> areasMap,
                    OSMSCOUT_HASHMAP<osmscout::FileOffset, osmscout::WayRef>  waysMap)
{
  switch (object.GetType())
  {
  case osmscout::refNode:
    return nodesMap[object.GetFileOffset()]->GetName();
    break;
  case osmscout::refArea:
    return areasMap[object.GetFileOffset()]->rings.front().GetName();
    break;
  case osmscout::refWay:
    return waysMap[object.GetFileOffset()]->GetName();
    break;
  default:
    return "";
  }

  return "";
}

int main(int argc, char* argv[])
{
  std::string                      map;
  std::string                      area;
  std::string                      location;
  std::list<osmscout::AdminRegion> areas;
  bool                             limitReached;

  if (argc!=3 && argc!=4) {
    std::cerr << "AddressLookup <map directory> [location] <area>" << std::endl;
    return 1;
  }

  map=argv[1];

  if (argc==4) {
    location=argv[2];
    area=argv[3];
  }
  else {
    area=argv[2];
  }


  osmscout::DatabaseParameter databaseParameter;
  osmscout::Database          database(databaseParameter);

  if (!database.Open(map.c_str())) {
    std::cerr << "Cannot open database" << std::endl;

    return 1;
  }

  if (location.empty()) {
    std::cout << "Looking for area '" << area << "'..." << std::endl;
  }
  else {
    std::cout << "Looking for location '" << location << "' in area '" << area << "'..." << std::endl;
  }

  if (!database.GetMatchingAdminRegions(area,areas,RESULT_SET_MAX_SIZE,limitReached,false)) {
    std::cerr << "Error while accessing database, quitting..." << std::endl;
    database.Close();
    return 1;
  }

  if (limitReached) {
    std::cerr << "To many hits for area, quitting..." << std::endl;
    database.Close();
    return 1;
  }

  for (std::list<osmscout::AdminRegion>::const_iterator area=areas.begin();
      area!=areas.end();
      ++area) {
    std::list<osmscout::Location>                             locations;

    std::set<osmscout::ObjectFileRef>                         objects;
    OSMSCOUT_HASHMAP<osmscout::FileOffset, osmscout::NodeRef> nodesMap;
    OSMSCOUT_HASHMAP<osmscout::FileOffset, osmscout::AreaRef> areasMap;
    OSMSCOUT_HASHMAP<osmscout::FileOffset, osmscout::WayRef>  waysMap;

    if (!location.empty()) {
      if (!database.GetMatchingLocations(*area,location,locations,RESULT_SET_MAX_SIZE,limitReached,false)) {
        std::cerr << "Error while accessing database, quitting..." << std::endl;
        database.Close();
        return 1;
      }

      if (limitReached) {
        std::cerr << "To many hits for area, quitting..." << std::endl;
        database.Close();
        return 1;
      }
    }

    objects.insert(area->reference);

    for (std::list<osmscout::Location>::const_iterator location=locations.begin();
        location!=locations.end();
        ++location) {
      for (std::list<osmscout::ObjectFileRef>::const_iterator reference=location->references.begin();
          reference!=location->references.end();
          ++reference) {

        objects.insert(*reference);
      }
    }

    if (!database.GetObjects(objects,
                             nodesMap,
                             areasMap,
                             waysMap)) {
      std::cerr << "Error while resolving locations" << std::endl;
      continue;
    }

    std::cout << "+ " << area->name;

    if (!area->path.empty()) {
      std::cout << " (" << osmscout::StringListToString(area->path) << ")";
    }

    std::cout << std::endl;

    std::cout << "  = " << GetName(area->reference,nodesMap,areasMap,waysMap) << " " << area->reference.GetTypeName() << " " << area->reference.GetFileOffset() << std::endl;

    for (std::list<osmscout::Location>::const_iterator location=locations.begin();
        location!=locations.end();
        ++location) {
      std::cout <<"  - " << location->name;

      if (!location->path.empty()) {
        std::cout << " (" << osmscout::StringListToString(location->path) << ")";
      }

      std::cout << std::endl;

      for (std::list<osmscout::ObjectFileRef>::const_iterator reference=location->references.begin();
          reference!=location->references.end();
          ++reference) {
        std::cout << "    = " << GetName(*reference,nodesMap,areasMap,waysMap) << " " << reference->GetTypeName() << " " << reference->GetFileOffset() << std::endl;
      }
    }
  }

  database.Close();

  return 0;
}
