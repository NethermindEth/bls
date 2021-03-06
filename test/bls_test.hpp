#include <bls/bls.hpp>
#include <cybozu/test.hpp>
#include <cybozu/inttype.hpp>
#include <cybozu/benchmark.hpp>
#include <cybozu/sha2.hpp>
#include <cybozu/atoi.hpp>
#include <cybozu/file.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>

typedef std::vector<uint8_t> Uint8Vec;
Uint8Vec fromHexStr(const std::string& s)
{
	Uint8Vec ret(s.size() / 2);
	for (size_t i = 0; i < s.size(); i += 2) {
		ret[i / 2] = cybozu::hextoi(&s[i], 2);
	}
	return ret;
}

template<class T>
void streamTest(const T& t)
{
	std::ostringstream oss;
	oss << t;
	std::istringstream iss(oss.str());
	T t2;
	iss >> t2;
	CYBOZU_TEST_EQUAL(t, t2);
}

template<class T>
void testSetForBN254()
{
	/*
		mask value to be less than r if the value >= (1 << (192 + 62))
	*/
	const uint64_t fff = uint64_t(-1);
	const uint64_t one = uint64_t(1);
	const struct {
		uint64_t in;
		uint64_t expected;
	} tbl[] = {
		{ fff, (one << 61) - 1 }, // masked with (1 << 61) - 1
		{ one << 62, 0 }, // masked
		{ (one << 62) | (one << 61), (one << 61) }, // masked
		{ (one << 61) - 1, (one << 61) - 1 }, // same
	};
	T t1, t2;
	for (size_t i = 0; i < CYBOZU_NUM_OF_ARRAY(tbl); i++) {
		uint64_t v1[] = { fff, fff, fff, tbl[i].in };
		uint64_t v2[] = { fff, fff, fff, tbl[i].expected };
		t1.set(v1);
		t2.set(v2);
		CYBOZU_TEST_EQUAL(t1, t2);
	}
}

void testForBN254()
{
	CYBOZU_TEST_EQUAL(bls::getOpUnitSize(), 4);
	bls::Id id;
	CYBOZU_TEST_ASSERT(id.isZero());
	id = 5;
	CYBOZU_TEST_EQUAL(id, 5);
	{
		const uint64_t id1[] = { 1, 2, 3, 4 };
		id.set(id1);
		std::ostringstream os;
		os << id;
		CYBOZU_TEST_EQUAL(os.str(), "0x4000000000000000300000000000000020000000000000001");
	}
	testSetForBN254<bls::Id>();
	testSetForBN254<bls::SecretKey>();
}

void setFp2Serialize(char s[96])
{
	mclBnFp r;
	mclBnFp_setByCSPRNG(&r);
	mclBnFp_serialize(s, 48, &r);
	mclBnFp_setByCSPRNG(&r);
	mclBnFp_serialize(s + 48, 48, &r);
}

void hashTest(int type)
{
#ifdef BLS_ETH
	if (type != MCL_BLS12_381) return;
	bls::SecretKey sec;
	sec.init();
	bls::PublicKey pub;
	sec.getPublicKey(pub);
	char h[96];
	setFp2Serialize(h);
	bls::Signature sig;
	sec.signHash(sig, h, sizeof(h));
	CYBOZU_TEST_ASSERT(sig.verifyHash(pub, h, sizeof(h)));
	CYBOZU_TEST_ASSERT(!sig.verifyHash(pub, "\x01\x02\04"));
#else
	bls::SecretKey sec;
	sec.init();
	bls::PublicKey pub;
	sec.getPublicKey(pub);
	const std::string h = "\x01\x02\x03";
	bls::Signature sig;
	sec.signHash(sig, h);
	CYBOZU_TEST_ASSERT(sig.verifyHash(pub, h));
	CYBOZU_TEST_ASSERT(!sig.verifyHash(pub, "\x01\x02\04"));
	if (type == MCL_BN254) {
		CYBOZU_TEST_EXCEPTION(sec.signHash(sig, "", 0), std::exception);
		CYBOZU_TEST_EXCEPTION(sec.signHash(sig, "\x00", 1), std::exception);
		CYBOZU_TEST_EXCEPTION(sec.signHash(sig, "\x00\x00", 2), std::exception);
#ifndef BLS_SWAP_G
		const uint64_t c1[] = { 0x0c00000000000004ull, 0xcf0f000000000006ull, 0x26cd890000000003ull, 0x2523648240000001ull };
		const uint64_t mc1[] = { 0x9b0000000000000full, 0x921200000000000dull, 0x9366c48000000004ull };
		CYBOZU_TEST_EXCEPTION(sec.signHash(sig, c1, 32), std::exception);
		CYBOZU_TEST_EXCEPTION(sec.signHash(sig, mc1, 24), std::exception);
#endif
	}
#endif
}

void blsTest()
{
	bls::SecretKey sec;
	sec.init();
	streamTest(sec);
	bls::PublicKey pub;
	sec.getPublicKey(pub);
	streamTest(pub);
	for (int i = 0; i < 5; i++) {
		std::string m = "hello";
		m += char('0' + i);
		bls::Signature sig;
		sec.sign(sig, m);
		CYBOZU_TEST_ASSERT(sig.verify(pub, m));
		CYBOZU_TEST_ASSERT(!sig.verify(pub, m + "a"));
		streamTest(sig);
		CYBOZU_BENCH_C("sign", 300, sec.sign, sig, m);
		CYBOZU_BENCH_C("verify", 300, sig.verify, pub, m);
	}
}

void k_of_nTest()
{
	const std::string m = "abc";
	const int n = 5;
	const int k = 3;
	bls::SecretKey sec0;
	sec0.init();
	bls::Signature sig0;
	sec0.sign(sig0, m);
	bls::PublicKey pub0;
	sec0.getPublicKey(pub0);
	CYBOZU_TEST_ASSERT(sig0.verify(pub0, m));

	bls::SecretKeyVec msk;
	sec0.getMasterSecretKey(msk, k);

	bls::SecretKeyVec allPrvVec(n);
	bls::IdVec allIdVec(n);
	for (int i = 0; i < n; i++) {
		int id = i + 1;
		allPrvVec[i].set(msk, id);
		allIdVec[i] = id;

		bls::SecretKey p;
		p.set(msk.data(), k, id);
		CYBOZU_TEST_EQUAL(allPrvVec[i], p);
	}

	bls::SignatureVec allSigVec(n);
	for (int i = 0; i < n; i++) {
		CYBOZU_TEST_ASSERT(allPrvVec[i] != sec0);
		allPrvVec[i].sign(allSigVec[i], m);
		bls::PublicKey pub;
		allPrvVec[i].getPublicKey(pub);
		CYBOZU_TEST_ASSERT(pub != pub0);
		CYBOZU_TEST_ASSERT(allSigVec[i].verify(pub, m));
	}

	/*
		3-out-of-n
		can recover
	*/
	bls::SecretKeyVec secVec(3);
	bls::IdVec idVec(3);
	for (int a = 0; a < n; a++) {
		secVec[0] = allPrvVec[a];
		idVec[0] = allIdVec[a];
		for (int b = a + 1; b < n; b++) {
			secVec[1] = allPrvVec[b];
			idVec[1] = allIdVec[b];
			for (int c = b + 1; c < n; c++) {
				secVec[2] = allPrvVec[c];
				idVec[2] = allIdVec[c];
				bls::SecretKey sec;
				sec.recover(secVec, idVec);
				CYBOZU_TEST_EQUAL(sec, sec0);
				bls::SecretKey sec2;
				sec2.recover(secVec.data(), idVec.data(), secVec.size());
				CYBOZU_TEST_EQUAL(sec, sec2);
			}
		}
	}
	{
		secVec[0] = allPrvVec[0];
		secVec[1] = allPrvVec[1];
		secVec[2] = allPrvVec[0]; // same of secVec[0]
		idVec[0] = allIdVec[0];
		idVec[1] = allIdVec[1];
		idVec[2] = allIdVec[0];
		bls::SecretKey sec;
		CYBOZU_TEST_EXCEPTION_MESSAGE(sec.recover(secVec, idVec), std::exception, "same id");
	}
	{
		/*
			n-out-of-n
			can recover
		*/
		bls::SecretKey sec;
		sec.recover(allPrvVec, allIdVec);
		CYBOZU_TEST_EQUAL(sec, sec0);
	}
	/*
		2-out-of-n
		can't recover
	*/
	secVec.resize(2);
	idVec.resize(2);
	for (int a = 0; a < n; a++) {
		secVec[0] = allPrvVec[a];
		idVec[0] = allIdVec[a];
		for (int b = a + 1; b < n; b++) {
			secVec[1] = allPrvVec[b];
			idVec[1] = allIdVec[b];
			bls::SecretKey sec;
			sec.recover(secVec, idVec);
			CYBOZU_TEST_ASSERT(sec != sec0);
		}
	}
	/*
		3-out-of-n
		can recover
	*/
	bls::SignatureVec sigVec(3);
	idVec.resize(3);
	for (int a = 0; a < n; a++) {
		sigVec[0] = allSigVec[a];
		idVec[0] = allIdVec[a];
		for (int b = a + 1; b < n; b++) {
			sigVec[1] = allSigVec[b];
			idVec[1] = allIdVec[b];
			for (int c = b + 1; c < n; c++) {
				sigVec[2] = allSigVec[c];
				idVec[2] = allIdVec[c];
				bls::Signature sig;
				sig.recover(sigVec, idVec);
				CYBOZU_TEST_EQUAL(sig, sig0);
			}
		}
	}
	{
		sigVec[0] = allSigVec[1]; idVec[0] = allIdVec[1];
		sigVec[1] = allSigVec[4]; idVec[1] = allIdVec[4];
		sigVec[2] = allSigVec[3]; idVec[2] = allIdVec[3];
		bls::Signature sig;
		CYBOZU_BENCH_C("sig.recover", 100, sig.recover, sigVec, idVec);
	}
	{
		/*
			n-out-of-n
			can recover
		*/
		bls::Signature sig;
		sig.recover(allSigVec, allIdVec);
		CYBOZU_TEST_EQUAL(sig, sig0);
	}
	/*
		2-out-of-n
		can't recover
	*/
	sigVec.resize(2);
	idVec.resize(2);
	for (int a = 0; a < n; a++) {
		sigVec[0] = allSigVec[a];
		idVec[0] = allIdVec[a];
		for (int b = a + 1; b < n; b++) {
			sigVec[1] = allSigVec[b];
			idVec[1] = allIdVec[b];
			bls::Signature sig;
			sig.recover(sigVec, idVec);
			CYBOZU_TEST_ASSERT(sig != sig0);
		}
	}
	// return same value if n = 1
	sigVec.resize(1);
	idVec.resize(1);
	sigVec[0] = allSigVec[0];
	idVec[0] = allIdVec[0];
	{
		bls::Signature sig;
		sig.recover(sigVec, idVec);
		CYBOZU_TEST_EQUAL(sig, sigVec[0]);
	}
	// share and recover publicKey
	{
		bls::PublicKeyVec pubVec(k);
		idVec.resize(k);
		// select [0, k) publicKey
		for (int i = 0; i < k; i++) {
			allPrvVec[i].getPublicKey(pubVec[i]);
			idVec[i] = allIdVec[i];
		}
		bls::PublicKey pub;
		pub.recover(pubVec, idVec);
		CYBOZU_TEST_EQUAL(pub, pub0);
		bls::PublicKey pub2;
		pub2.recover(pubVec.data(), idVec.data(), pubVec.size());
		CYBOZU_TEST_EQUAL(pub, pub2);
	}
}

void popTest()
{
	const size_t k = 3;
	const size_t n = 6;
	const std::string m = "pop test";
	bls::SecretKey sec0;
	sec0.init();
	bls::PublicKey pub0;
	sec0.getPublicKey(pub0);
	bls::Signature sig0;
	sec0.sign(sig0, m);
	CYBOZU_TEST_ASSERT(sig0.verify(pub0, m));

	bls::SecretKeyVec msk;
	sec0.getMasterSecretKey(msk, k);

	bls::PublicKeyVec mpk;
	bls::getMasterPublicKey(mpk, msk);
	bls::SignatureVec  popVec;
	bls::getPopVec(popVec, msk);

	for (size_t i = 0; i < popVec.size(); i++) {
		CYBOZU_TEST_ASSERT(popVec[i].verify(mpk[i]));
	}

	const int idTbl[n] = {
		3, 5, 193, 22, 15
	};
	bls::SecretKeyVec secVec(n);
	bls::PublicKeyVec pubVec(n);
	bls::SignatureVec sVec(n);
	for (size_t i = 0; i < n; i++) {
		int id = idTbl[i];
		secVec[i].set(msk, id);
		secVec[i].getPublicKey(pubVec[i]);
		bls::PublicKey pub;
		pub.set(mpk, id);
		CYBOZU_TEST_EQUAL(pubVec[i], pub);

		bls::Signature pop;
		secVec[i].getPop(pop);
		CYBOZU_TEST_ASSERT(pop.verify(pubVec[i]));

		secVec[i].sign(sVec[i], m);
		CYBOZU_TEST_ASSERT(sVec[i].verify(pubVec[i], m));
	}
	secVec.resize(k);
	sVec.resize(k);
	bls::IdVec idVec(k);
	for (size_t i = 0; i < k; i++) {
		idVec[i] = idTbl[i];
	}
	bls::SecretKey sec;
	sec.recover(secVec, idVec);
	CYBOZU_TEST_EQUAL(sec, sec0);
	bls::Signature sig;
	sig.recover(sVec, idVec);
	CYBOZU_TEST_EQUAL(sig, sig0);
	bls::Signature sig2;
	sig2.recover(sVec.data(), idVec.data(), sVec.size());
	CYBOZU_TEST_EQUAL(sig, sig2);
}

void addTest()
{
	bls::SecretKey sec1, sec2;
	sec1.init();
	sec2.init();
	CYBOZU_TEST_ASSERT(sec1 != sec2);

	bls::PublicKey pub1, pub2;
	sec1.getPublicKey(pub1);
	sec2.getPublicKey(pub2);

	const std::string m = "doremi";
	bls::Signature sig1, sig2;
	sec1.sign(sig1, m);
	sec2.sign(sig2, m);
	CYBOZU_TEST_ASSERT((sig1 + sig2).verify(pub1 + pub2, m));
}

void aggregateTest()
{
	const size_t n = 10;
	bls::SecretKey secs[n];
	bls::PublicKey pubs[n], pub;
	bls::Signature sigs[n], sig;
	const std::string m = "abc";
	for (size_t i = 0; i < n; i++) {
		secs[i].init();
		secs[i].getPublicKey(pubs[i]);
		secs[i].sign(sigs[i], m);
	}
	pub = pubs[0];
	sig = sigs[0];
	for (size_t i = 1; i < n; i++) {
		pub.add(pubs[i]);
		sig.add(sigs[i]);
	}
	CYBOZU_TEST_ASSERT(sig.verify(pub, m));
	blsSignature sig2;
	const blsSignature *p = (const blsSignature *)&sigs[0];
	blsAggregateSignature(&sig2, p, n);
	CYBOZU_TEST_ASSERT(blsSignatureIsEqual(&sig2, (const blsSignature*)&sig));
	memset(&sig, 0, sizeof(sig));
	blsAggregateSignature((blsSignature*)&sig2, 0, 0);
	CYBOZU_TEST_ASSERT(blsSignatureIsEqual(&sig2, (const blsSignature*)&sig));
}

void dataTest()
{
	const size_t FrSize = bls::getFrByteSize();
	const size_t FpSize = bls::getG1ByteSize();
	bls::SecretKey sec;
	sec.init();
	std::string str;
	sec.getStr(str, bls::IoFixedByteSeq);
	{
		CYBOZU_TEST_EQUAL(str.size(), FrSize);
		bls::SecretKey sec2;
		sec2.setStr(str, bls::IoFixedByteSeq);
		CYBOZU_TEST_EQUAL(sec, sec2);
	}
	bls::PublicKey pub;
	sec.getPublicKey(pub);
	pub.getStr(str, bls::IoFixedByteSeq);
	{
#ifdef BLS_SWAP_G
		CYBOZU_TEST_EQUAL(str.size(), FpSize);
#else
		CYBOZU_TEST_EQUAL(str.size(), FpSize * 2);
#endif
		bls::PublicKey pub2;
		pub2.setStr(str, bls::IoFixedByteSeq);
		CYBOZU_TEST_EQUAL(pub, pub2);
	}
	std::string m = "abc";
	bls::Signature sign;
	sec.sign(sign, m);
	sign.getStr(str, bls::IoFixedByteSeq);
	{
#ifdef BLS_SWAP_G
		CYBOZU_TEST_EQUAL(str.size(), FpSize * 2);
#else
		CYBOZU_TEST_EQUAL(str.size(), FpSize);
#endif
		bls::Signature sign2;
		sign2.setStr(str, bls::IoFixedByteSeq);
		CYBOZU_TEST_EQUAL(sign, sign2);
	}
	bls::Id id;
	const uint64_t v[] = { 1, 2, 3, 4, 5, 6, };
	id.set(v);
	id.getStr(str, bls::IoFixedByteSeq);
	{
		CYBOZU_TEST_EQUAL(str.size(), FrSize);
		bls::Id id2;
		id2.setStr(str, bls::IoFixedByteSeq);
		CYBOZU_TEST_EQUAL(id, id2);
	}
}

void testAggregatedHashes(size_t n)
{
#ifdef BLS_ETH
	const size_t sizeofHash = 48 * 2;
#else
	const size_t sizeofHash = 32;
#endif
	struct Hash { char data[sizeofHash]; };
	std::vector<bls::PublicKey> pubs(n);
	std::vector<bls::Signature> sigs(n);
	std::vector<Hash> h(n);
	for (size_t i = 0; i < n; i++) {
		bls::SecretKey sec;
		sec.init();
#ifdef BLS_ETH
		setFp2Serialize(h[i].data);
#else
		char msg[128];
		CYBOZU_SNPRINTF(msg, sizeof(msg), "abc-%d", (int)i);
		const size_t msgSize = strlen(msg);
		cybozu::Sha256().digest(h[i].data, sizeofHash, msg, msgSize);
#endif
		sec.getPublicKey(pubs[i]);
		sec.signHash(sigs[i], h[i].data, sizeofHash);
	}
	bls::Signature sig = sigs[0];
	for (size_t i = 1; i < n; i++) {
		sig.add(sigs[i]);
	}
	CYBOZU_TEST_ASSERT(sig.verifyAggregatedHashes(pubs.data(), h.data(), sizeofHash, n));
	bls::Signature invalidSig = sigs[0] + sigs[0];
	CYBOZU_TEST_ASSERT(!invalidSig.verifyAggregatedHashes(pubs.data(), h.data(), sizeofHash, n));
#ifndef BLS_ETH
	h[0].data[0]++;
	CYBOZU_TEST_ASSERT(!sig.verifyAggregatedHashes(pubs.data(), h.data(), sizeofHash, n));
#endif
	printf("n=%2d ", (int)n);
	CYBOZU_BENCH_C("aggregate", 50, sig.verifyAggregatedHashes, pubs.data(), h.data(), sizeofHash, n);
}

void verifyAggregateTest(int type)
{
	(void)type;
#ifdef BLS_ETH
	if (type != MCL_BLS12_381) return;
#endif
	const size_t nTbl[] = { 1, 2, 15, 16, 17, 50 };
	for (size_t i = 0; i < CYBOZU_NUM_OF_ARRAY(nTbl); i++) {
		testAggregatedHashes(nTbl[i]);
	}
}

unsigned int writeSeq(void *self, void *buf, unsigned int bufSize)
{
	int& seq = *(int*)self;
	char *p = (char *)buf;
	for (unsigned int i = 0; i < bufSize; i++) {
		p[i] = char(seq++);
	}
	return bufSize;
}

void setRandFuncTest(int type)
{
	(void)type;
#ifdef BLS_ETH
	if (type == MCL_BLS12_381) return;
#endif
	blsSecretKey sec;
	const int seqInit1 = 5;
	int seq = seqInit1;
	blsSetRandFunc(&seq, writeSeq);
	blsSecretKeySetByCSPRNG(&sec);
	unsigned char buf[128];
	size_t n = blsSecretKeySerialize(buf, sizeof(buf), &sec);
	CYBOZU_TEST_ASSERT(n > 0);
	for (size_t i = 0; i < n - 1; i++) {
		// ommit buf[n - 1] because it may be masked
		CYBOZU_TEST_EQUAL(buf[i], seqInit1 + i);
	}
	// use default CSPRNG
	blsSetRandFunc(0, 0);
	blsSecretKeySetByCSPRNG(&sec);
	n = blsSecretKeySerialize(buf, sizeof(buf), &sec);
	CYBOZU_TEST_ASSERT(n > 0);
	printf("sec=");
	for (size_t i = 0; i < n; i++) {
		printf("%02x", buf[i]);
	}
	printf("\n");
}

#if BLS_ETH
void ethAggregateVerifyNoCheckTest()
{
	puts("ethAggregateVerifyNoCheckTest");
	// https://media.githubusercontent.com/media/ethereum/eth2.0-spec-tests/v0.10.1/tests/general/phase0/bls/aggregate_verify/small/fast_aggregate_verify_valid/data.yaml
	const struct Tbl {
		const char *pub;
		const char *msg;
	} tbl[] = {
		{
			"a491d1b0ecd9bb917989f0e74f0dea0422eac4a873e5e2644f368dffb9a6e20fd6e10c1b77654d067c0618f6e5a7f79a",
			"0000000000000000000000000000000000000000000000000000000000000000",
		},
		{
			"b301803f8b5ac4a1133581fc676dfedc60d891dd5fa99028805e5ea5b08d3491af75d0707adab3b70c6a6a580217bf81",
			"5656565656565656565656565656565656565656565656565656565656565656",
		},
		{
			"b53d21a4cfd562c469cc81514d4ce5a6b577d8403d32a394dc265dd190b47fa9f829fdd7963afdf972e5e77854051f6f",
			"abababababababababababababababababababababababababababababababab",
		}
	};
	const char *sigStr = "82f5bfe5550ce639985a46545e61d47c5dd1d5e015c1a82e20673698b8e02cde4f81d3d4801f5747ad8cfd7f96a8fe50171d84b5d1e2549851588a5971d52037218d4260b9e4428971a5c1969c65388873f1c49a4c4d513bdf2bc478048a18a8";
	const size_t n = CYBOZU_NUM_OF_ARRAY(tbl);
	bls::Signature sig;
	bls::PublicKey pubVec[n];
	Uint8Vec msgVec;
	sig.deserializeHexStr(sigStr);
	size_t msgSize = 0;
	for (size_t i = 0; i < n; i++) {
		pubVec[i].deserializeHexStr(tbl[i].pub);
		const Uint8Vec t = fromHexStr(tbl[i].msg);
		if (i == 0) msgSize = t.size();
		msgVec.insert(msgVec.end(), t.begin(), t.end());
	}
	CYBOZU_TEST_EQUAL(blsAggregateVerifyNoCheck(sig.getPtr(), pubVec[0].getPtr(), msgVec.data(), msgSize, n), 1);
}

void ethAggregateTest()
{
	puts("ethAggregateTest");
	// https://media.githubusercontent.com/media/ethereum/eth2.0-spec-tests/v0.10.1/tests/general/phase0/bls/aggregate/small/aggregate_0x0000000000000000000000000000000000000000000000000000000000000000/data.yaml
	const struct {
		const char *s;
	} tbl[] = {
		{ "b2a0bd8e837fc2a1b28ee5bcf2cddea05f0f341b375e51de9d9ee6d977c2813a5c5583c19d4e7db8d245eebd4e502163076330c988c91493a61b97504d1af85fdc167277a1664d2a43af239f76f176b215e0ee81dc42f1c011dc02d8b0a31e32" },
		{ "b2deb7c656c86cb18c43dae94b21b107595486438e0b906f3bdb29fa316d0fc3cab1fc04c6ec9879c773849f2564d39317bfa948b4a35fc8509beafd3a2575c25c077ba8bca4df06cb547fe7ca3b107d49794b7132ef3b5493a6ffb2aad2a441" },
		{ "a1db7274d8981999fee975159998ad1cc6d92cd8f4b559a8d29190dad41dc6c7d17f3be2056046a8bcbf4ff6f66f2a360860fdfaefa91b8eca875d54aca2b74ed7148f9e89e2913210a0d4107f68dbc9e034acfc386039ff99524faf2782de0e" },
	};
	const char *expect = "973ab0d765b734b1cbb2557bcf52392c9c7be3cd21d5bd28572d99f618c65e921f0dd82560cc103feb9f000c23c00e660e1364ed094f137e1045e73116cd75903af446df3c357540a4970ec367a7f7fa7493a5db27ca322c48d57740908585e8";
	const size_t n = CYBOZU_NUM_OF_ARRAY(tbl);
	bls::Signature sig;
	sig.clear();
	for (size_t i = 0; i < n; i++) {
		bls::Signature t;
		t.deserializeHexStr(tbl[i].s);
		sig.add(t);
	}
	CYBOZU_TEST_EQUAL(sig.serializeToHexStr(), expect);
}

void ethSignOneTest(const std::string& secHex, const std::string& msgHex, const std::string& sigHex)
{
	const Uint8Vec msg = fromHexStr(msgHex);
	bls::SecretKey sec;
	sec.setStr(secHex, 16);
	bls::PublicKey pub;
	sec.getPublicKey(pub);
	bls::Signature sig;
	sec.sign(sig, msg.data(), msg.size());
	CYBOZU_TEST_EQUAL(sig.serializeToHexStr(), sigHex);
	CYBOZU_TEST_ASSERT(sig.verify(pub, msg.data(), msg.size()));
}

void ethSignFileTest(const std::string& dir)
{
	std::string fileName = cybozu::GetExePath() + "../test/eth/" + dir + "/sign.txt";
	std::ifstream ifs(fileName.c_str());
	CYBOZU_TEST_ASSERT(ifs);
	for (;;) {
		std::string h1, h2, h3, sec, msg, sig;
		ifs >>  h1 >> sec >> h2 >> msg >> h3 >> sig;
		if (h1.empty()) break;
		if (h1 != "sec" || h2 != "msg" || h3 != "out") {
			throw cybozu::Exception("bad format") << fileName << h1 << h2 << h3;
		}
		ethSignOneTest(sec, msg, sig);
	}
}

void ethSignTest()
{
	puts("ethSignTest");
	ethSignFileTest("draft05");
	const char *secHex = "47b8192d77bf871b62e87859d653922725724a5c031afeabc60bcef5ff665138";
	const char *msgHex = "0000000000000000000000000000000000000000000000000000000000000000";
	const char *sigHex = "b2deb7c656c86cb18c43dae94b21b107595486438e0b906f3bdb29fa316d0fc3cab1fc04c6ec9879c773849f2564d39317bfa948b4a35fc8509beafd3a2575c25c077ba8bca4df06cb547fe7ca3b107d49794b7132ef3b5493a6ffb2aad2a441";
	ethSignOneTest(secHex, msgHex, sigHex);
}

void ethFastAggregateVerifyTest(const std::string& dir)
{
	puts("ethFastAggregateVerifyTest");
	std::string fileName = cybozu::GetExePath() + "../test/eth/" + dir + "/fast_aggregate_verify.txt";
	std::ifstream ifs(fileName.c_str());
	CYBOZU_TEST_ASSERT(ifs);
	int i = 0;
	for (;;) {
		std::vector<bls::PublicKey> pubVec;
		Uint8Vec msg;
		bls::Signature sig;
		int output;
		std::string h;
		std::string s;
		for (;;) {
			ifs >> h;
			if (h.empty()) return;
			if (h != "pub") break;
			bls::PublicKey pub;
			ifs >> s;
			pub.deserializeHexStr(s);
			pubVec.push_back(pub);
		}
		printf("i=%d\n", i++);
		if (h != "msg") throw cybozu::Exception("bad msg") << h;
		ifs >> s;
		msg = fromHexStr(s);
		ifs >> h;
		if (h != "sig") throw cybozu::Exception("bad sig") << h;
		ifs >> s;
		try {
			sig.deserializeHexStr(s);
			CYBOZU_TEST_EQUAL(blsSignatureIsValidOrder(sig.getPtr()), 1);
		} catch (...) {
			printf("bad signature %s\n", s.c_str());
			sig.clear();
		}
		ifs >> h;
		if (h != "out") throw cybozu::Exception("bad out") << h;
		ifs >> s;
		if (s == "false") {
			output = 0;
		} else if (s == "true") {
			output = 1;
		} else {
			throw cybozu::Exception("bad out") << s;
		}
		int r = blsFastAggregateVerify(sig.getPtr(), pubVec[0].getPtr(), pubVec.size(), msg.data(), msg.size());
		CYBOZU_TEST_EQUAL(r, output);
	}
}

void blsAggregateVerifyNoCheckTestOne(size_t n)
{
	const size_t msgSize = 32;
	std::vector<bls::PublicKey> pubs(n);
	std::vector<bls::Signature> sigs(n);
	std::string msgs(msgSize * n, 0);
	for (size_t i = 0; i < n; i++) {
		bls::SecretKey sec;
		sec.init();
		sec.getPublicKey(pubs[i]);
		msgs[msgSize * i] = i;
		sec.sign(sigs[i], &msgs[msgSize * i], msgSize);
	}
	blsSignature aggSig;
	blsAggregateSignature(&aggSig, sigs[0].getPtr(), n);
	CYBOZU_TEST_EQUAL(blsAggregateVerifyNoCheck(&aggSig, pubs[0].getPtr(), msgs.data(), msgSize, n), 1);
	CYBOZU_BENCH_C("blsAggregateVerifyNoCheck", 50, blsAggregateVerifyNoCheck, &aggSig, pubs[0].getPtr(), msgs.data(), msgSize, n);
	(*(char*)(&aggSig))++;
	CYBOZU_TEST_EQUAL(blsAggregateVerifyNoCheck(&aggSig, pubs[0].getPtr(), msgs.data(), msgSize, n), 0);
}

void blsAggregateVerifyNoCheckTest()
{
	const size_t nTbl[] = { 1, 2, 15, 16, 17, 50 };
	for (size_t i = 0; i < CYBOZU_NUM_OF_ARRAY(nTbl); i++) {
		blsAggregateVerifyNoCheckTestOne(nTbl[i]);
	}
}

void draft06Test()
{
	blsSetETHmode(BLS_ETH_MODE_DRAFT_06);
	blsSecretKey sec;
	blsSecretKeySetHexStr(&sec, "1", 1);
	blsSignature sig;
	const char *msg = "asdf";
	const char *tbl[] = {
		"991465766822328609851486184896183909315973720876657478886869638351620419080108037412710821468345199867495830514994",
		"1927263325419177785864064254809595520594843896432194052293468762304708262511397472768048460101768845190689994385404",
		"1809070181727662187520244137990122973104312257969329378620821268587650918986396248285565202085536393521872124028279",
		"422840987629306440608451989474855096319159701852700504738670150565612981489044166427550429014138733315907834328002",
	};
	blsSign(&sig, &sec, msg, strlen(msg));
	mclBnG2_normalize(&sig.v, &sig.v);
	const mclBnFp *p = &sig.v.x.d[0];
	for (int i = 0; i < 4; i++) {
		char buf[128];
		mclBnFp_getStr(buf, sizeof(buf), &p[i], 10);
		CYBOZU_TEST_EQUAL(buf, tbl[i]);
	}
}

void draft07Test()
{
	blsSetETHmode(BLS_ETH_MODE_DRAFT_07);
	blsSecretKey sec;
	blsSecretKeySetHexStr(&sec, "1", 1);
	blsSignature sig;
	const char *msg = "asdf";
	const char *tbl[] = {
		"2525875563870715639912451285996878827057943937903727288399283574780255586622124951113038778168766058972461529282986",
		"3132482115871619853374334004070359337604487429071253737901486558733107203612153024147084489564256619439711974285977",
		"2106640002084734620850657217129389007976098691731730501862206029008913488613958311385644530040820978748080676977912",
		"2882649322619140307052211460282445786973517746532934590265600680988689024512167659295505342688129634612479405019290",
	};
	blsSign(&sig, &sec, msg, strlen(msg));
	mclBnG2_normalize(&sig.v, &sig.v);
	{
		const bls::Signature& b = *(const bls::Signature*)(&sig);
		printf("draft07-sig(%s) by sec=1 =%s\n", msg, b.serializeToHexStr().c_str());
	}
	const mclBnFp *p = &sig.v.x.d[0];
	for (int i = 0; i < 4; i++) {
		char buf[128];
		mclBnFp_getStr(buf, sizeof(buf), &p[i], 10);
		CYBOZU_TEST_EQUAL(buf, tbl[i]);
	}
}

void ethTest(int type)
{
	if (type != MCL_BLS12_381) return;
	blsSetETHmode(BLS_ETH_MODE_DRAFT_05);
	ethAggregateTest();
	ethSignTest();
	ethAggregateVerifyNoCheckTest();
	ethFastAggregateVerifyTest("draft05");
	blsAggregateVerifyNoCheckTest();
	draft06Test();
	draft07Test();
}
#endif

void testAll(int type)
{
#if 1
	blsTest();
	k_of_nTest();
	popTest();
	addTest();
	dataTest();
	aggregateTest();
	verifyAggregateTest(type);
	setRandFuncTest(type);
	hashTest(type);
#endif
#ifdef BLS_ETH
	ethTest(type);
#endif
}
CYBOZU_TEST_AUTO(all)
{
	const struct {
		int type;
		const char *name;
	} tbl[] = {
		{ MCL_BN254, "BN254" },
#if MCLBN_FP_UNIT_SIZE == 6 && MCLBN_FR_UNIT_SIZE == 6
		{ MCL_BN381_1, "BN381_1" },
#endif
#if MCLBN_FP_UNIT_SIZE == 6 && MCLBN_FR_UNIT_SIZE == 4
		{ MCL_BLS12_381, "BLS12_381" },
#endif
	};
	for (size_t i = 0; i < CYBOZU_NUM_OF_ARRAY(tbl); i++) {
		printf("curve=%s\n", tbl[i].name);
		int type = tbl[i].type;
		bls::init(type);
		if (type == MCL_BN254) {
			testForBN254();
		}
		testAll(type);
	}
}
