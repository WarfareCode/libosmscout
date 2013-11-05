/*
  This source is part of the libosmscout library
  Copyright (C) 2009  Tim Teulings

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
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include <osmscout/CityStreetIndex.h>

#include <iostream>

#include <osmscout/system/Assert.h>

#include <osmscout/util/File.h>
#include <osmscout/util/StopClock.h>

namespace osmscout {

  const char* const CityStreetIndex::FILENAME_REGION_DAT     = "region.dat";
  const char* const CityStreetIndex::FILENAME_NAMEREGION_IDX = "nameregion.idx";

  CityStreetIndex::LocationVisitor::LocationVisitor(FileScanner& scanner)
  : startWith(false),
    limit(50),
    limitReached(false),
    hashFunction(NULL),
    scanner(scanner)
  {
    // no code
  }

  bool CityStreetIndex::LocationVisitor::Visit(const std::string& locationName,
                                               const Loc &loc)
  {
    std::string::size_type pos;
    bool                   found;

    if (hashFunction!=NULL) {
      std::string hash=(*hashFunction)(locationName);

      if (!hash.empty()) {
        pos=hash.find(nameHash);
      }
      else {
        pos=locationName.find(name);
      }
    }
    else {
      pos=locationName.find(name);
    }

    if (startWith) {
      found=pos==0;
    }
    else {
      found=pos!=std::string::npos;
    }

    if (!found) {
      return true;
    }

    if (locations.size()>=limit) {
      limitReached=true;

      return false;
    }

    Location location;

    location.name=locationName;

    for (std::vector<ObjectFileRef>::const_iterator object=loc.objects.begin();
         object!=loc.objects.end();
         ++object) {
      location.references.push_back(*object);
    }

    // Build up path for each hit by following
    // the parent relation up to the top of the tree.

    FileOffset currentOffset;
    FileOffset regionOffset=loc.offset;

    if (!scanner.GetPos(currentOffset)) {
      return false;
    }

    while (!scanner.HasError() &&
           regionOffset!=0) {
      std::string name;

      scanner.SetPos(regionOffset);
      scanner.Read(name);
      scanner.ReadNumber(regionOffset);

      if (location.path.empty()) {
        // We do not want something like "'Dortmund' in 'Dortmund'"!
        if (name!=locationName) {
          location.path.push_back(name);
        }
      }
      else {
        location.path.push_back(name);
      }
    }

    locations.push_back(location);

    return scanner.SetPos(currentOffset);
  }

  CityStreetIndex::CityStreetIndex()
   : hashFunction(NULL)
  {
    // no code
  }

  CityStreetIndex::~CityStreetIndex()
  {
    // no code
  }

  bool CityStreetIndex::LoadRegion(FileScanner& scanner,
                                   LocationVisitor& visitor) const
  {
    FileOffset                offset;
    std::string               name;
    FileOffset                parentOffset;
    uint32_t                  childrenCount;
    uint32_t                  locationCount;
    std::map<std::string,Loc> locations;

    if (!scanner.GetPos(offset) ||
      !scanner.Read(name) ||
      !scanner.ReadNumber(parentOffset)) {
      return false;
    }

    if (!scanner.ReadNumber(childrenCount)) {
      return false;
    }

    for (size_t i=0; i<childrenCount; i++) {
      if (!LoadRegion(scanner,
                      visitor)) {
        return false;
      }
    }

    if (!scanner.ReadNumber(locationCount)) {
      return false;
    }

    for (size_t i=0; i<locationCount; i++) {
      std::string name;
      uint32_t    objectCount;

      if (!scanner.Read(name)) {
        return false;
      }

      locations[name].offset=offset;

      if (!scanner.ReadNumber(objectCount)) {
        return false;
      }

      locations[name].objects.reserve(objectCount);

      FileOffset lastOffset=0;

      for (size_t j=0; j<objectCount; j++) {
        uint8_t    type;
        FileOffset offset;

        if (!scanner.Read(type)) {
          return false;
        }

        if (!scanner.ReadNumber(offset)) {
          return false;
        }

        offset+=lastOffset;

        locations[name].objects.push_back(ObjectFileRef(offset,(RefType)type));

        lastOffset=offset;
      }
    }

    for (std::map<std::string,Loc>::const_iterator l=locations.begin();
         l!=locations.end();
         ++l) {
      if (!visitor.Visit(l->first,l->second)) {
        return true;
      }
    }

    return !scanner.HasError();
  }

  bool CityStreetIndex::LoadRegion(FileScanner& scanner,
                                   FileOffset offset,
                                   LocationVisitor& visitor) const
  {
    scanner.SetPos(offset);

    return LoadRegion(scanner,visitor);
  }

  bool CityStreetIndex::Load(const std::string& path,
                             std::string (*hashFunction) (std::string))
  {
    this->path=path;
    this->hashFunction=hashFunction;

    return true;
  }


  bool CityStreetIndex::GetMatchingAdminRegions(const std::string& name,
                                                std::list<AdminRegion>& regions,
                                                size_t limit,
                                                bool& limitReached,
                                                bool startWith) const
  {
    std::string nameHash;

    limitReached=false;
    regions.clear();

    // if the user supplied a special hash function call it and use the result
    if (hashFunction!=NULL) {
      nameHash=(*hashFunction)(name);
    }

    FileScanner   scanner;

    if (!scanner.Open(AppendFileToDir(path,
                                      FILENAME_NAMEREGION_IDX),
                      FileScanner::LowMemRandom,
                      false)) {
      std::cerr << "Cannot open file '" << scanner.GetFilename() << "'!" << std::endl;
      return false;
    }

    uint32_t regionCount;

    if (!scanner.ReadNumber(regionCount)) {
      return false;
    }

    for (size_t i=0; i<regionCount; i++) {
      std::string regionName;
      uint32_t    entries;

      if (!scanner.Read(regionName)) {
        return false;
      }

      if (!scanner.ReadNumber(entries)) {
        return false;
      }

      FileOffset lastOffset=0;

      for (size_t j=0; j<entries; j++) {
        Region     region;
        uint8_t    type;
        FileOffset offset;

        region.name=regionName;

        if (!scanner.Read(type)) {
          return false;
        }

        if (!scanner.ReadNumber(offset)) {
          return false;
        }

        offset+=lastOffset;

        lastOffset=offset;

        region.reference.Set(offset,(RefType)type);

        if (!scanner.ReadFileOffset(region.offset)) {
          return false;
        }

        bool                   found=false;
        std::string::size_type matchPosition;

        // Calculate match

        if (hashFunction!=NULL) {
          std::string hash=(*hashFunction)(region.name);

          if (!hash.empty()) {
            matchPosition=hash.find(nameHash);
          }
          else {
            matchPosition=region.name.find(name);
          }
        }
        else {
          matchPosition=region.name.find(name);
        }

        if (startWith) {
          found=matchPosition==0;
        }
        else {
          found=matchPosition!=std::string::npos;
        }

        // If match, Add to result

        if (found) {
          if (regions.size()>=limit) {
            limitReached=true;
          }
          else {
            AdminRegion adminRegion;

            adminRegion.reference=region.reference;
            adminRegion.offset=region.offset;
            adminRegion.name=region.name;

            regions.push_back(adminRegion);
          }
        }
      }
    }

    if (!scanner.Close()) {
      return false;
    }

    if (regions.empty()) {
      return true;
    }

    if (!regions.empty()) {
      if (!scanner.Open(AppendFileToDir(path,
                                        FILENAME_REGION_DAT),
                        FileScanner::LowMemRandom,
                        true)) {
        std::cerr << "Cannot open file '" << scanner.GetFilename() << "'!" << std::endl;
        return false;
      }

      // If there are results, build up a path for each hit by following
      // the parent relation up to the top of the tree.

      for (std::list<AdminRegion>::iterator area=regions.begin();
           area!=regions.end();
           ++area) {
        FileOffset offset=area->offset;

        while (offset!=0) {
          std::string name;

          scanner.SetPos(offset);
          scanner.Read(name);
          scanner.ReadNumber(offset);

          if (area->path.empty()) {
            if (name!=area->name) {
              area->path.push_back(name);
            }
          }
          else {
            area->path.push_back(name);
          }
        }
      }

      return !scanner.HasError() && scanner.Close();
    }

    return true;
  }

  bool CityStreetIndex::GetMatchingLocations(const AdminRegion& region,
                                             const std::string& name,
                                             std::list<Location>& locations,
                                             size_t limit,
                                             bool& limitReached,
                                             bool startWith) const
  {
    FileScanner scanner;

    if (!scanner.Open(AppendFileToDir(path,
                                      FILENAME_REGION_DAT),
                      FileScanner::LowMemRandom,
                      true)) {
      std::cerr << "Cannot open file '" << scanner.GetFilename() << "'!" << std::endl;
      return false;
    }

    LocationVisitor locVisitor(scanner);

    locVisitor.name=name;
    locVisitor.startWith=startWith;
    locVisitor.limit=limit;
    locVisitor.limitReached=false;

    locVisitor.hashFunction=hashFunction;

    if (hashFunction!=NULL) {
      locVisitor.nameHash=(*hashFunction)(name);
    }


    if (!LoadRegion(locVisitor.scanner,
                    region.offset,
                    locVisitor)) {
      return false;
    }

    locations=locVisitor.locations;
    limitReached=locVisitor.limitReached;

    return true;
  }

  void CityStreetIndex::DumpStatistics()
  {
    size_t memory=0;

    std::cout << "CityStreetIndex: Memory " << memory << std::endl;
  }
}

