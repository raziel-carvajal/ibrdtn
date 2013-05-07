/* $Id: templateengine.py 2241 2006-05-22 07:58:58Z fischer $ */

///
/// @file        BundleStorageTest.cpp
/// @brief       CPPUnit-Tests for class SimpleBundleStorage
/// @author      Author Name (email@mail.address)
/// @date        Created at 2010-11-01
/// 
/// @version     $Revision: 2241 $
/// @note        Last modification: $Date: 2006-05-22 09:58:58 +0200 (Mon, 22 May 2006) $
///              by $Author: fischer $
///

/*
 * Copyright (C) 2011 IBR, TU Braunschweig
 *
 * Written-by: Johannes Morgenroth <morgenroth@ibr.cs.tu-bs.de>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "config.h"
#include "BundleStorageTest.hh"
#include "../tools/TestEventListener.h"
#include <cppunit/extensions/HelperMacros.h>

#include <ibrdtn/data/Bundle.h>
#include <ibrdtn/data/EID.h>
#include <ibrcommon/thread/Thread.h>
#include "core/TimeEvent.h"
#include <ibrdtn/utils/Clock.h>
#include "core/BundleCore.h"
#include <ibrcommon/data/File.h>
#include <ibrcommon/data/BLOB.h>
#include <ibrcommon/thread/MutexLock.h>
#include <ibrdtn/data/PayloadBlock.h>
#include <ibrdtn/data/AgeBlock.h>
#include "Component.h"

#include "storage/SimpleBundleStorage.h"
#include "storage/MemoryBundleStorage.h"

#ifdef HAVE_SQLITE
#include "storage/SQLiteBundleStorage.h"
#endif

#include <unistd.h>

CPPUNIT_TEST_SUITE_REGISTRATION(BundleStorageTest);

/*========================== setup / teardown ==========================*/

size_t BundleStorageTest::testCounter;
dtn::storage::BundleStorage* BundleStorageTest::_storage = NULL;
std::vector<std::string> BundleStorageTest::_storage_names;

void BundleStorageTest::setUp()
{
	// create a new event switch
	esl = new ibrtest::EventSwitchLoop();

	// enable blob path
	ibrcommon::File blob_path("/tmp/blobs");

	// check if the BLOB path exists
	if (!blob_path.exists()) {
		// try to create the BLOB path
		ibrcommon::File::createDirectory(blob_path);
	}

	// enable the blob provider
	ibrcommon::BLOB::changeProvider(new ibrcommon::FileBLOBProvider(blob_path), true);

	switch (testCounter++)
	{
	case 0:
		{
			// add standard memory base storage
			_storage = new dtn::storage::MemoryBundleStorage();
			break;
		}

	case 1:
		{
			// prepare path for the disk based storage
			ibrcommon::File path("/tmp/bundle-disk-test");
			if (path.exists()) path.remove(true);
			ibrcommon::File::createDirectory(path);

			// add disk based storage
			_storage = new dtn::storage::SimpleBundleStorage(path);
			break;
		}

#ifdef HAVE_SQLITE
	case 2:
		{
			// prepare path for the sqlite based storage
			ibrcommon::File path("/tmp/bundle-sqlite-test");
			if (path.exists()) path.remove(true);
			ibrcommon::File::createDirectory(path);

			// prepare a sqlite database
			_storage = new dtn::storage::SQLiteBundleStorage(path, 0);
			break;
		}
#endif
	}

	if (testCounter >= _storage_names.size()) testCounter = 0;

	// start-up event switch
	esl->start();

	try {
		dtn::daemon::Component &c = dynamic_cast<dtn::daemon::Component&>(*_storage);
		c.initialize();
		c.startup();
	} catch (const bad_cast&) {
	}
}

void BundleStorageTest::tearDown()
{
	esl->stop();

	try {
		dtn::daemon::Component &c = dynamic_cast<dtn::daemon::Component&>(*_storage);
		c.terminate();
	} catch (const bad_cast&) {
	}

	esl->join();
	delete esl;
	esl = NULL;

	// delete storage
	delete _storage;
}

#define STORAGE_TEST(f) f(*_storage);

/*========================== tests below ==========================*/

void BundleStorageTest::testStore()
{
	STORAGE_TEST(testStore);
}

void BundleStorageTest::testStore(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)0, storage.count());

	storage.store(b);

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)1, storage.count());
}

void BundleStorageTest::testRemove()
{
	STORAGE_TEST(testRemove);
}

void BundleStorageTest::testRemove(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)0, storage.count());

	storage.store(b);

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)1, storage.count());

	CPPUNIT_ASSERT_NO_THROW_MESSAGE( "storage.remove(b)", storage.remove(b) );

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)0, storage.count());
}

void BundleStorageTest::testAgeBlock()
{
	STORAGE_TEST(testAgeBlock);
}

void BundleStorageTest::testAgeBlock(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;

	// set standard variable.sourceurce = dtn::data::EID("dtn://node-one/test");
	b.lifetime = 1;
	b.destination = dtn::data::EID("dtn://node-two/test");

	// add some payload
	ibrcommon::BLOB::Reference ref = ibrcommon::BLOB::create();
	b.push_back(ref);

	(*ref.iostream()) << "Hallo Welt" << std::endl;

	{
		dtn::data::AgeBlock &agebl = b.push_back<dtn::data::AgeBlock>();
		agebl.setSeconds(42);
	}

	// store the bundle
	storage.store(b);

	// special case for caching storages (SimpleBundleStorage)
	// wait until the bundle is written
	storage.wait();

	dtn::data::BundleID id(b);

	// clear the bundle
	b = dtn::data::Bundle();

	try {
		// retrieve the bundle
		CPPUNIT_ASSERT_NO_THROW_MESSAGE( "storage.get(id)", b = storage.get(id) );

		// get the ageblock
		dtn::data::AgeBlock &agebl = b.find<dtn::data::AgeBlock>();

		CPPUNIT_ASSERT(agebl.getSeconds() >= 42);
	} catch (const dtn::data::Bundle::NoSuchBlockFoundException&) {
		CPPUNIT_FAIL("AgeBlock missing");
	}
}

void BundleStorageTest::testClear()
{
	STORAGE_TEST(testClear);
}

void BundleStorageTest::testClear(dtn::storage::BundleStorage &storage)
{
	/* test signature () */
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)0, storage.count());

	storage.store(b);

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)1, storage.count());

	storage.clear();

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)0, storage.count());
}

void BundleStorageTest::testEmpty()
{
	STORAGE_TEST(testEmpty);
}

void BundleStorageTest::testEmpty(dtn::storage::BundleStorage &storage)
{
	/* test signature () */
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	CPPUNIT_ASSERT_EQUAL(true, storage.empty());

	storage.store(b);

	CPPUNIT_ASSERT_EQUAL(false, storage.empty());

	storage.clear();

	CPPUNIT_ASSERT_EQUAL(true, storage.empty());
}

void BundleStorageTest::testCount()
{
	STORAGE_TEST(testCount);
}

void BundleStorageTest::testCount(dtn::storage::BundleStorage &storage)
{
	/* test signature () */
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)0, storage.count());

	storage.store(b);

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)1, storage.count());
}

void BundleStorageTest::testSize()
{
	STORAGE_TEST(testSize);
}

void BundleStorageTest::testSize(dtn::storage::BundleStorage &storage)
{
	/* test signature () const */
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	ibrcommon::BLOB::Reference ref = ibrcommon::BLOB::create();
	b.push_back(ref);

	(*ref.iostream()) << "Hallo Welt" << std::endl;

	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)0, storage.size());

	storage.store(b);

	try {
		dynamic_cast<dtn::storage::SQLiteBundleStorage&>(storage);
		const dtn::data::PayloadBlock &p = b.find<dtn::data::PayloadBlock>();

		// TODO: fix .size() implementation in SQLiteBundleStorage
		//CPPUNIT_ASSERT((dtn::data::Length)p.getLength() <= storage.size());
	} catch (const std::bad_cast&) {
		std::stringstream ss;
		dtn::data::DefaultSerializer(ss) << b;

		CPPUNIT_ASSERT_EQUAL((dtn::data::Length)ss.str().length(), storage.size());
	}
}

void BundleStorageTest::testReleaseCustody()
{
	STORAGE_TEST(testReleaseCustody);
}

void BundleStorageTest::testReleaseCustody(dtn::storage::BundleStorage &storage)
{
	/* test signature (dtn::data::BundleID &bundle) */
	dtn::data::BundleID id;
	dtn::data::EID eid;
	storage.releaseCustody(eid, id);
}

void BundleStorageTest::testRaiseEvent()
{
	STORAGE_TEST(testRaiseEvent);
}

void BundleStorageTest::testRaiseEvent(dtn::storage::BundleStorage &storage)
{
		/* test signature (const Event *evt) */
	dtn::data::EID eid("dtn://node-one/test");
	dtn::data::Bundle b;
	b.lifetime = 60;
	b.source = dtn::data::EID("dtn://node-two/foo");

	storage.store(b);
	storage.wait();
	dtn::core::TimeEvent::raise(dtn::utils::Clock::getTime() + 3600, dtn::utils::Clock::getTime() + 3600 - dtn::utils::Clock::TIMEVAL_CONVERSION, dtn::core::TIME_SECOND_TICK);
}

void BundleStorageTest::testConcurrentStoreGet()
{
	STORAGE_TEST(testConcurrentStoreGet);
}

void BundleStorageTest::testConcurrentStoreGet(dtn::storage::BundleStorage &storage)
{
	class StoreProcess : public ibrcommon::JoinableThread
	{
	public:
		StoreProcess(dtn::storage::BundleStorage &storage, std::list<dtn::data::Bundle> &list)
		: _storage(storage), _list(list)
		{ };

		virtual ~StoreProcess()
		{
			join();
		};

		void __cancellation() throw ()
		{
		}

	protected:
		void run() throw ()
		{
			for (std::list<dtn::data::Bundle>::const_iterator iter = _list.begin(); iter != _list.end(); ++iter)
			{
				const dtn::data::Bundle &b = (*iter);
				_storage.store(b);
			}
		}

	private:
		dtn::storage::BundleStorage &_storage;
		std::list<dtn::data::Bundle> &_list;
	};

	// create some bundles
	std::list<dtn::data::Bundle> list1, list2;

	for (int i = 0; i < 2000; ++i)
	{
		dtn::data::Bundle b;
		b.lifetime = 1;
		b.source = dtn::data::EID("dtn://node-two/foo");
		ibrcommon::BLOB::Reference ref = ibrcommon::BLOB::create();
		b.push_back(ref);

		(*ref.iostream()) << "Hallo Welt" << std::endl;

		if ((i % 3) == 0) list1.push_back(b); else list2.push_back(b);
	}

	// store list1 in the storage
	{ StoreProcess sp(storage, list1); sp.start(); sp.stop(); }

	// store list1 in the storage
	{
		StoreProcess sp(storage, list2);
		sp.start();

		// in parallel we try to retrieve bundles from list1
		for (std::list<dtn::data::Bundle>::const_iterator iter = list1.begin(); iter != list1.end(); ++iter)
		{
			const dtn::data::Bundle &b = (*iter);
			dtn::data::Bundle bundle = storage.get(b);
			storage.remove(bundle);
		}

		sp.stop();
	}

	storage.clear();
}

void BundleStorageTest::testRestore()
{
	STORAGE_TEST(testRestore);
}

void BundleStorageTest::testRestore(dtn::storage::BundleStorage &storage)
{
	// exclude memory-storage since it does not support bundle restore
	try {
		dynamic_cast<dtn::storage::MemoryBundleStorage&>(storage);
		return;
	} catch (const std::bad_cast&) {

	};

	try {
		// we need control over the component to do this test
		dtn::daemon::Component &c = dynamic_cast<dtn::daemon::Component&>(storage);

		// create some bundles
		for (int i = 0; i < 2000; ++i)
		{
			dtn::data::Bundle b;
			b.lifetime = 1;
			b.source = dtn::data::EID("dtn://node-two/foo");
			ibrcommon::BLOB::Reference ref = ibrcommon::BLOB::create();
			b.push_back(ref);

			(*ref.iostream()) << "Hallo Welt" << std::endl;

			storage.store(b);
		}

		// shutdown the storage
		c.terminate();

		// check if the storage is empty now
		CPPUNIT_ASSERT_EQUAL(true, storage.empty());

		// reboot the storage system
		c.initialize();
		c.startup();

		// the storage should contain 2000 bundles
		CPPUNIT_ASSERT_EQUAL((dtn::data::Size)2000, storage.count());
	} catch (const std::bad_cast&) {

	};
}

void BundleStorageTest::testExpiration()
{
	STORAGE_TEST(testExpiration);
}

void BundleStorageTest::testExpiration(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");
	b.lifetime = 20;

	// store the bundle
	storage.store(b);

	// check if the storage count is right
	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)1, storage.count());

	TestEventListener<dtn::core::TimeEvent> evtl;

	// raise time event to trigger expiration
	dtn::core::TimeEvent::raise(b.timestamp + 21, dtn::utils::Clock::getUnixTimestamp(), TIME_SECOND_TICK);

	// wait until the time event has been processed
	{
		ibrcommon::MutexLock l(evtl.event_cond);
		while (evtl.event_counter == 0) evtl.event_cond.wait(20000);
	}

	// special case for storages deferred mechanisms (SimpleBundleStorage)
	// wait until all tasks of the storage are processed
	storage.wait();

	// check if the storage count is right
	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)0, storage.count());
}

void BundleStorageTest::testDistinctDestinations()
{
	STORAGE_TEST(testDistinctDestinations);
}

void BundleStorageTest::testDistinctDestinations(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	for (int i = 0; i < 10; i++) {
		b.relabel();

		std::stringstream ss;
		ss << "dtn://node-two/" << i;
		b.destination = dtn::data::EID(ss.str());

		// store the bundle
		storage.store(b);
	}

	for (int i = 5; i < 10; i++) {
		b.relabel();

		std::stringstream ss;
		ss << "dtn://node-two/" << i;
		b.destination = dtn::data::EID(ss.str());

		// store the bundle
		storage.store(b);
	}

	// check the number of distinct addresses
	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)10, storage.getDistinctDestinations().size());
}

void BundleStorageTest::testSelector()
{
	STORAGE_TEST(testSelector);
}

void BundleStorageTest::testSelector(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	for (int i = 0; i < 10; i++) {
		b.relabel();

		std::stringstream ss;
		ss << "dtn://node-two/" << i;
		b.destination = dtn::data::EID(ss.str());

		// store the bundle
		storage.store(b);
	}

	for (int i = 5; i < 10; i++) {
		b.relabel();

		std::stringstream ss;
		ss << "dtn://node-two/" << i;
		b.destination = dtn::data::EID(ss.str());

		// store the bundle
		storage.store(b);
	}

	class BundleFilter : public dtn::storage::BundleSelector
	{
	public:
		BundleFilter()
		{};

		virtual ~BundleFilter() {};

		virtual dtn::data::Size limit() const throw () { return 5; };

		virtual bool shouldAdd(const dtn::data::MetaBundle &meta) const throw (dtn::storage::BundleSelectorException)
		{
			// select every second bundle
			return (meta.destination == dtn::data::EID("dtn://node-two/6"));
		};
	};

	dtn::storage::BundleResultList list;

	// create a new bundle filter
	BundleFilter filter;

	// query an unknown bundle from the storage, the list contains max. 10 items.
	list.clear();
	storage.get(filter, list);

	// check the number of selected bundles
	CPPUNIT_ASSERT_EQUAL((size_t)2, list.size());
}

void BundleStorageTest::testRemoveBloomfilter()
{
	STORAGE_TEST(testRemoveBloomfilter);
}

void BundleStorageTest::testRemoveBloomfilter(dtn::storage::BundleStorage &storage)
{
	std::vector<dtn::data::Bundle> bundles;

	for (int i = 0; i < 20; i++) {
		dtn::data::Bundle b;
		b.source = dtn::data::EID("dtn://node-one/test");

		std::stringstream ss;
		ss << "dtn://node-two/" << i;
		b.destination = dtn::data::EID(ss.str());

		// copy bundle into the list of bundles
		bundles.push_back(b);

		// store the bundle
		storage.store(b);
	}

	ibrcommon::BloomFilter bf;
	bf.insert(bundles[1].toString());
	bf.insert(bundles[4].toString());
	bf.insert(bundles[11].toString());

	// remove the bundles marked in the bloom-filter
	CPPUNIT_ASSERT_THROW( while (true) storage.remove(bf), dtn::storage::NoBundleFoundException );

	// check the number of bundles in the storage
	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)17, storage.count());
}

void BundleStorageTest::testDoubleStore()
{
	STORAGE_TEST(testDoubleStore);
}

void BundleStorageTest::testDoubleStore(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;
	b.source = dtn::data::EID("dtn://node-one/test");

	for (int i = 0; i < 2; i++) {
		std::stringstream ss;
		ss << "dtn://node-two/" << i;
		b.destination = dtn::data::EID(ss.str());

		// store the bundle
		storage.store(b);
	}

	// check the number of selected bundles
	CPPUNIT_ASSERT_EQUAL((dtn::data::Size)1, storage.count());
}

void BundleStorageTest::testFaultyGet()
{
	STORAGE_TEST(testFaultyGet);
}

void BundleStorageTest::testFaultyGet(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;

	// set standard variables
	b.source = dtn::data::EID("dtn://node-one/test");
	b.lifetime = 1;
	b.destination = dtn::data::EID("dtn://node-two/test");

	// add some payload
	ibrcommon::BLOB::Reference ref = ibrcommon::BLOB::create();
	b.push_back(ref);

	(*ref.iostream()) << "Hallo Welt" << std::endl;

	// store the bundle
	storage.store(b);

	// special case for caching storages (SimpleBundleStorage)
	// wait until the bundle is written
	storage.wait();

	dtn::data::BundleID id(b);

	// clear the bundle
	b = dtn::data::Bundle();

	// set the storage to faulty
	storage.setFaulty(true);

	// retrieve the bundle
	CPPUNIT_ASSERT_THROW( b = storage.get(id), dtn::storage::NoBundleFoundException );

	// set the storage to non-faulty
	storage.setFaulty(false);
}

void BundleStorageTest::testFaultyStore()
{
	STORAGE_TEST(testFaultyStore);
}

void BundleStorageTest::testFaultyStore(dtn::storage::BundleStorage &storage)
{
	dtn::data::Bundle b;

	// set standard variables
	b.source = dtn::data::EID("dtn://node-one/test");
	b.lifetime = 1;
	b.destination = dtn::data::EID("dtn://node-two/test");

	// add some payload
	ibrcommon::BLOB::Reference ref = ibrcommon::BLOB::create();
	b.push_back(ref);

	(*ref.iostream()) << "Hallo Welt" << std::endl;

	// set the storage to faulty
	storage.setFaulty(true);

	// store the bundle
	storage.store(b);

	// special case for caching storages (SimpleBundleStorage)
	// wait until the bundle is written
	storage.wait();

	// set the storage to non-faulty
	storage.setFaulty(false);

	dtn::data::BundleID id(b);

	// clear the bundle
	b = dtn::data::Bundle();

	// retrieve the bundle
	CPPUNIT_ASSERT_THROW( b = storage.get(id), dtn::storage::NoBundleFoundException );
}
