////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2019 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
////////////////////////////////////////////////////////////////////////////////


#include "Basics/Common.h"
#include "Basics/FileUtils.h"
#include "Basics/StringBuffer.h"
#include "Basics/files.h"
#include "Random/RandomGenerator.h"
#include "RocksDBEngine/RocksDBEventListener.h"

#include "catch.hpp"
#include <string>
#include <iostream>

using namespace arangodb::basics;

static bool Initialized = false;
static uint64_t counter = 0;

// -----------------------------------------------------------------------------
// --SECTION--                                                 setup / tear-down
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// CFilesSetup class copied from tests/Basic/files-test.cpp
////////////////////////////////////////////////////////////////////////////////
struct CFilesSetup {
  CFilesSetup () : _directory(true) {
    long systemError;
    std::string errorMessage;

    if (!Initialized) {
      Initialized = true;
      arangodb::RandomGenerator::initialize(arangodb::RandomGenerator::RandomType::MERSENNE);
    }

    _directory.appendText(TRI_GetTempPath());
    _directory.appendChar(TRI_DIR_SEPARATOR_CHAR);
    _directory.appendText("arangotest-");
    _directory.appendInteger(static_cast<uint64_t>(TRI_microtime()));
    _directory.appendInteger(arangodb::RandomGenerator::interval(UINT32_MAX));

    TRI_CreateDirectory(_directory.c_str(), systemError, errorMessage);
  }

  ~CFilesSetup () {
    // let's be sure we delete the right stuff
    TRI_ASSERT(_directory.length() > 10);

    TRI_RemoveDirectory(_directory.c_str());
  }

  StringBuffer* writeFile (const char* blob) {
    StringBuffer* filename = new StringBuffer(true);
    filename->appendText(_directory);
    filename->appendChar(TRI_DIR_SEPARATOR_CHAR);
    filename->appendText("tmp-");
    filename->appendInteger(++counter);
    filename->appendInteger(arangodb::RandomGenerator::interval(UINT32_MAX));

    FILE* fd = fopen(filename->c_str(), "wb");

    if (fd) {
      size_t numWritten = fwrite(blob, strlen(blob), 1, fd);
      (void) numWritten;
      fclose(fd);
    }
    else {
      CHECK(false == true);
    }

    return filename;
  }

  StringBuffer * writeFile (const char * name, const char * blob) {
    StringBuffer* filename = new StringBuffer(true);
    filename->appendText(_directory);
    filename->appendChar(TRI_DIR_SEPARATOR_CHAR);
    filename->appendText(name);

    FILE* fd = fopen(filename->c_str(), "wb");

    if (fd) {
      size_t numWritten = fwrite(blob, strlen(blob), 1, fd);
      (void) numWritten;
      fclose(fd);
    }
    else {
      CHECK(false == true);
    }

    return filename;
  }

  StringBuffer _directory;
};


// -----------------------------------------------------------------------------
// --SECTION--                                                        action tests
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

TEST_CASE("RocksDBEventListenerThread statics", "[rocks][devel]") {

SECTION("sha a new file") {
  CFilesSetup s;
  std::string new_sst;
  bool good;
  int ret_val;

  new_sst=s._directory.c_str();
  new_sst+= TRI_DIR_SEPARATOR_CHAR;
  new_sst+="000042.sst";

  ret_val = TRI_WriteFile(new_sst.c_str(), "the quick brown fox", 19);
  CHECK(TRI_ERROR_NO_ERROR == ret_val);

  good = arangodb::RocksDBEventListenerThread::shaCalcFile(new_sst.c_str());
  CHECK(true == good);

}

SECTION("delete matching sha") {
  CFilesSetup s;
  std::string new_sst, basepath, new_sha;
  bool good;
  int ret_val;

  basepath=s._directory.c_str();
  basepath+= TRI_DIR_SEPARATOR_CHAR;
  basepath+="000069";
  new_sst = basepath + ".sst";

  ret_val = TRI_WriteFile(new_sst.c_str(), "12345 67890 12345 67890", 23);
  CHECK(TRI_ERROR_NO_ERROR == ret_val);

  /// not real ssh for the data written above
  new_sha = basepath + ".sha.e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  ret_val = TRI_WriteFile(new_sha.c_str(), "", 0);
  CHECK(TRI_ERROR_NO_ERROR == ret_val);

  good = arangodb::RocksDBEventListenerThread::deleteFile(new_sst.c_str());
  CHECK(true == good);

}
} // TEST_CASE


////////////////////////////////////////////////////////////////////////////////
/// @brief test checkMissingShaFiles scenarios
////////////////////////////////////////////////////////////////////////////////
class TestRocksDBEventListenerThread : public arangodb::RocksDBEventListenerThread {
public:
  TestRocksDBEventListenerThread() : arangodb::RocksDBEventListenerThread("testListener") {
    // sample sha values are simulated, not real
    setup.writeFile("MANIFEST-000004", "some manifest data");
    setup.writeFile("CURRENT","MANIFEST-000004\n");
    setup.writeFile("IDENTITY","no idea what goes here");
    setup.writeFile("037793.sst","raw data 1");
    setup.writeFile("037793.sha.e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","");
    setup.writeFile("037684.sst","raw data 2");
    setup.writeFile("086218.sha.e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","");
    setup.writeFile("086219.sst","raw data 3");
  };

  CFilesSetup setup;

  virtual std::string getRocksDBPath() {return setup._directory.c_str();}

  std::string pathName(const char * name) {
    std::string retpath;
    retpath = setup._directory.c_str();
    retpath += TRI_DIR_SEPARATOR_CHAR;
    retpath += name;
    return retpath;
  };
};


TEST_CASE("checkMissingShaFiles simple", "[rocks][devel]") {
SECTION("verify common situations") {
  TestRocksDBEventListenerThread tr;

  tr.checkMissingShaFiles(tr.setup._directory.c_str());

  CHECK( TRI_ExistsFile(tr.pathName("MANIFEST-000004").c_str()));
  CHECK( TRI_ExistsFile(tr.pathName("CURRENT").c_str()));
  CHECK( TRI_ExistsFile(tr.pathName("IDENTITY").c_str()));
  CHECK( TRI_ExistsFile(tr.pathName("037793.sst").c_str()));
  CHECK( TRI_ExistsFile(tr.pathName("037793.sha.e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855").c_str()));
  CHECK( TRI_ExistsFile(tr.pathName("037684.sst").c_str()));
  CHECK( TRI_ExistsFile(tr.pathName("037684.sha.2db3c4a7da801356e4efda0d65229d0baadf6950b366418e96abb7ece9c56c12").c_str()));
  CHECK( TRI_ExistsFile(tr.pathName("086219.sst").c_str()));
  CHECK( TRI_ExistsFile(tr.pathName("086219.sha.5d3cfa346c3852c0c108d720d580cf99910749f17c8429c07c1c2d714be2b7ff").c_str()));

  CHECK( !TRI_ExistsFile(tr.pathName("086218.sha.e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855").c_str()));


} // SECTION
} // TEST_CASE
