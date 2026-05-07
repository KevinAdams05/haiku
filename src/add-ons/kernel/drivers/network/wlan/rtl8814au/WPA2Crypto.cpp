/*
 * WPA2Crypto.cpp — kernel-side crypto primitives for in-driver WPA2.
 *
 * SHA-1 and HMAC-SHA1: RFC 3174 / RFC 2104 reference implementations.
 * AES-128: compact textbook implementation using S-box + inverse S-box;
 *   no precomputed T-tables to keep the static-data footprint small.
 * PRF-384: IEEE 802.11i §8.5.1.1.
 * AES key unwrap: RFC 3394 §2.2.2.
 *
 * No allocations, no globals other than const tables.
 */


#include "WPA2Crypto.h"

#include <KernelExport.h>
#include <string.h>


namespace wpa2_crypto {


// ---------------------------------------------------------------------------
// SHA-1 (RFC 3174)
// ---------------------------------------------------------------------------

struct Sha1Ctx {
	uint32	state[5];
	uint64	bitCount;
	uint8	buffer[64];
};


static inline uint32
rotl32(uint32 x, uint32 n)
{
	return (x << n) | (x >> (32 - n));
}


static void
sha1_transform(uint32 state[5], const uint8 block[64])
{
	uint32 W[80];
	for (uint32 i = 0; i < 16; i++) {
		W[i] = ((uint32)block[i * 4] << 24)
			| ((uint32)block[i * 4 + 1] << 16)
			| ((uint32)block[i * 4 + 2] << 8)
			| (uint32)block[i * 4 + 3];
	}
	for (uint32 i = 16; i < 80; i++)
		W[i] = rotl32(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);

	uint32 a = state[0], b = state[1], c = state[2];
	uint32 d = state[3], e = state[4];

	for (uint32 i = 0; i < 80; i++) {
		uint32 f, k;
		if (i < 20)      { f = (b & c) | ((~b) & d);          k = 0x5A827999; }
		else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDC; }
		else             { f = b ^ c ^ d;                     k = 0xCA62C1D6; }
		uint32 t = rotl32(a, 5) + f + e + k + W[i];
		e = d; d = c; c = rotl32(b, 30); b = a; a = t;
	}
	state[0] += a; state[1] += b; state[2] += c;
	state[3] += d; state[4] += e;
}


static void
sha1_init(Sha1Ctx* ctx)
{
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xEFCDAB89;
	ctx->state[2] = 0x98BADCFE;
	ctx->state[3] = 0x10325476;
	ctx->state[4] = 0xC3D2E1F0;
	ctx->bitCount = 0;
}


static void
sha1_update(Sha1Ctx* ctx, const uint8* data, uint32 len)
{
	uint32 i = (uint32)((ctx->bitCount >> 3) & 63);
	ctx->bitCount += (uint64)len << 3;
	if (i + len < 64) {
		memcpy(&ctx->buffer[i], data, len);
		return;
	}
	uint32 first = 64 - i;
	memcpy(&ctx->buffer[i], data, first);
	sha1_transform(ctx->state, ctx->buffer);
	uint32 offset = first;
	while (offset + 64 <= len) {
		sha1_transform(ctx->state, data + offset);
		offset += 64;
	}
	memcpy(ctx->buffer, data + offset, len - offset);
}


static void
sha1_final(Sha1Ctx* ctx, uint8 digest[20])
{
	uint64 bitLen = ctx->bitCount;
	uint32 i = (uint32)((ctx->bitCount >> 3) & 63);
	ctx->buffer[i++] = 0x80;
	if (i > 56) {
		while (i < 64)
			ctx->buffer[i++] = 0;
		sha1_transform(ctx->state, ctx->buffer);
		i = 0;
	}
	while (i < 56)
		ctx->buffer[i++] = 0;
	for (uint32 j = 0; j < 8; j++)
		ctx->buffer[56 + j] = (uint8)(bitLen >> (56 - j * 8));
	sha1_transform(ctx->state, ctx->buffer);
	for (uint32 j = 0; j < 5; j++) {
		digest[j * 4]     = (uint8)(ctx->state[j] >> 24);
		digest[j * 4 + 1] = (uint8)(ctx->state[j] >> 16);
		digest[j * 4 + 2] = (uint8)(ctx->state[j] >> 8);
		digest[j * 4 + 3] = (uint8)ctx->state[j];
	}
}


void
sha1(const uint8* data, uint32 len, uint8 digest[20])
{
	Sha1Ctx ctx;
	sha1_init(&ctx);
	sha1_update(&ctx, data, len);
	sha1_final(&ctx, digest);
}


void
hmac_sha1(const uint8* key, uint32 keyLen,
	const uint8* msg, uint32 msgLen, uint8 mac[20])
{
	uint8 keyBuf[64];
	if (keyLen > 64) {
		// Hash long keys down to 20 bytes per RFC 2104 §2.
		sha1(key, keyLen, keyBuf);
		memset(keyBuf + 20, 0, 44);
	} else {
		memcpy(keyBuf, key, keyLen);
		memset(keyBuf + keyLen, 0, 64 - keyLen);
	}
	uint8 ipad[64];
	uint8 opad[64];
	for (uint32 i = 0; i < 64; i++) {
		ipad[i] = keyBuf[i] ^ 0x36;
		opad[i] = keyBuf[i] ^ 0x5C;
	}
	Sha1Ctx ctx;
	uint8 inner[20];
	sha1_init(&ctx);
	sha1_update(&ctx, ipad, 64);
	sha1_update(&ctx, msg, msgLen);
	sha1_final(&ctx, inner);
	sha1_init(&ctx);
	sha1_update(&ctx, opad, 64);
	sha1_update(&ctx, inner, 20);
	sha1_final(&ctx, mac);
}


// ---------------------------------------------------------------------------
// PRF-384 (IEEE 802.11i §8.5.1.1)
// ---------------------------------------------------------------------------

void
prf_384(const uint8* key, uint32 keyLen,
	const char* label, uint32 labelLen,
	const uint8* data, uint32 dataLen,
	uint8 output[48])
{
	// Working buffer: label || 0x00 || data || counter.
	uint8 buf[256];
	uint32 bufLen = labelLen + 1 + dataLen + 1;
	if (bufLen > sizeof(buf)) {
		memset(output, 0, 48);
		return;
	}
	memcpy(buf, label, labelLen);
	buf[labelLen] = 0;
	memcpy(buf + labelLen + 1, data, dataLen);

	// PRF iterates HMAC-SHA1 with a single-byte counter at the end.
	// 384/160 = 2.4 -> 3 iterations, last truncated to 8 bytes.
	uint8 hash[20];
	for (uint8 i = 0; i < 3; i++) {
		buf[bufLen - 1] = i;
		hmac_sha1(key, keyLen, buf, bufLen, hash);
		uint32 chunk = (i < 2) ? 20 : 8;
		memcpy(output + i * 20, hash, chunk);
	}
}


// ---------------------------------------------------------------------------
// AES-128 (FIPS 197).  Compact reference using S-box + inverse S-box.
// ---------------------------------------------------------------------------

static const uint8 sBox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};


static const uint8 invSBox[256] = {
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};


static const uint8 rcon[11] = {
	0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};


// AES-128 has Nk=4, Nb=4, Nr=10.  Round-key schedule: 11 round keys
// of 16 bytes each = 176 bytes.
static void
aes128_key_expansion(const uint8 key[16], uint8 schedule[176])
{
	memcpy(schedule, key, 16);
	uint32 generated = 16;
	uint32 rconIdx = 1;
	uint8 t[4];
	while (generated < 176) {
		// Take last 4 bytes
		for (uint32 i = 0; i < 4; i++)
			t[i] = schedule[generated - 4 + i];
		if (generated % 16 == 0) {
			// RotWord + SubWord + Rcon
			uint8 tmp = t[0];
			t[0] = sBox[t[1]] ^ rcon[rconIdx++];
			t[1] = sBox[t[2]];
			t[2] = sBox[t[3]];
			t[3] = sBox[tmp];
		}
		for (uint32 i = 0; i < 4; i++) {
			schedule[generated] = schedule[generated - 16] ^ t[i];
			generated++;
		}
	}
}


// GF(2^8) multiplication used in MixColumns.
static inline uint8
xtime(uint8 b)
{
	return (uint8)((b << 1) ^ ((b & 0x80) ? 0x1b : 0));
}


static inline uint8
gmul(uint8 a, uint8 b)
{
	uint8 result = 0;
	for (uint32 i = 0; i < 8; i++) {
		if (b & 1)
			result ^= a;
		uint8 hi = a & 0x80;
		a <<= 1;
		if (hi)
			a ^= 0x1b;
		b >>= 1;
	}
	return result;
}


// Operate on a 4x4 column-major state.
static void
add_round_key(uint8 state[16], const uint8* roundKey)
{
	for (uint32 i = 0; i < 16; i++)
		state[i] ^= roundKey[i];
}


static void
sub_bytes(uint8 state[16])
{
	for (uint32 i = 0; i < 16; i++)
		state[i] = sBox[state[i]];
}


static void
inv_sub_bytes(uint8 state[16])
{
	for (uint32 i = 0; i < 16; i++)
		state[i] = invSBox[state[i]];
}


static void
shift_rows(uint8 s[16])
{
	uint8 t = s[1];   s[1] = s[5];   s[5] = s[9];   s[9] = s[13];  s[13] = t;
	t = s[2];   uint8 u = s[6];   s[2] = s[10];  s[6] = s[14]; s[10] = t;     s[14] = u;
	t = s[15];  s[15] = s[11];  s[11] = s[7];   s[7] = s[3];   s[3] = t;
}


static void
inv_shift_rows(uint8 s[16])
{
	uint8 t = s[13];  s[13] = s[9];  s[9] = s[5];  s[5] = s[1];  s[1] = t;
	t = s[2];   uint8 u = s[6];   s[2] = s[10];  s[6] = s[14]; s[10] = t;     s[14] = u;
	t = s[3];   s[3] = s[7];   s[7] = s[11];  s[11] = s[15]; s[15] = t;
}


static void
mix_columns(uint8 s[16])
{
	for (uint32 c = 0; c < 4; c++) {
		uint8 a0 = s[c * 4], a1 = s[c * 4 + 1];
		uint8 a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
		uint8 b0 = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3;
		uint8 b1 = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3;
		uint8 b2 = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3);
		uint8 b3 = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3);
		s[c * 4] = b0; s[c * 4 + 1] = b1;
		s[c * 4 + 2] = b2; s[c * 4 + 3] = b3;
	}
}


static void
inv_mix_columns(uint8 s[16])
{
	for (uint32 c = 0; c < 4; c++) {
		uint8 a0 = s[c * 4], a1 = s[c * 4 + 1];
		uint8 a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
		uint8 b0 = gmul(a0, 0x0e) ^ gmul(a1, 0x0b) ^ gmul(a2, 0x0d) ^ gmul(a3, 0x09);
		uint8 b1 = gmul(a0, 0x09) ^ gmul(a1, 0x0e) ^ gmul(a2, 0x0b) ^ gmul(a3, 0x0d);
		uint8 b2 = gmul(a0, 0x0d) ^ gmul(a1, 0x09) ^ gmul(a2, 0x0e) ^ gmul(a3, 0x0b);
		uint8 b3 = gmul(a0, 0x0b) ^ gmul(a1, 0x0d) ^ gmul(a2, 0x09) ^ gmul(a3, 0x0e);
		s[c * 4] = b0; s[c * 4 + 1] = b1;
		s[c * 4 + 2] = b2; s[c * 4 + 3] = b3;
	}
}


void
aes128_encrypt(const uint8 key[16], const uint8 plaintext[16],
	uint8 ciphertext[16])
{
	uint8 schedule[176];
	aes128_key_expansion(key, schedule);

	uint8 state[16];
	memcpy(state, plaintext, 16);

	add_round_key(state, schedule);
	for (uint32 round = 1; round < 10; round++) {
		sub_bytes(state);
		shift_rows(state);
		mix_columns(state);
		add_round_key(state, schedule + round * 16);
	}
	sub_bytes(state);
	shift_rows(state);
	add_round_key(state, schedule + 160);

	memcpy(ciphertext, state, 16);
}


void
aes128_decrypt(const uint8 key[16], const uint8 ciphertext[16],
	uint8 plaintext[16])
{
	uint8 schedule[176];
	aes128_key_expansion(key, schedule);

	uint8 state[16];
	memcpy(state, ciphertext, 16);

	add_round_key(state, schedule + 160);
	for (uint32 round = 9; round >= 1; round--) {
		inv_shift_rows(state);
		inv_sub_bytes(state);
		add_round_key(state, schedule + round * 16);
		inv_mix_columns(state);
	}
	inv_shift_rows(state);
	inv_sub_bytes(state);
	add_round_key(state, schedule);

	memcpy(plaintext, state, 16);
}


// ---------------------------------------------------------------------------
// RFC 3394 AES Key Wrap — unwrap only.
// ---------------------------------------------------------------------------

bool
aes_unwrap(const uint8 kek[16], const uint8* cipher, uint32 cipherLen,
	uint8* plaintext)
{
	if (cipherLen < 16 || (cipherLen % 8) != 0)
		return false;
	uint32 n = (cipherLen / 8) - 1;	// number of plaintext blocks (8 bytes each)
	if (n < 1)
		return false;

	uint8 A[8];
	memcpy(A, cipher, 8);
	memcpy(plaintext, cipher + 8, n * 8);

	uint8 block[16];
	for (int j = 5; j >= 0; j--) {
		for (int i = (int)n; i >= 1; i--) {
			uint64 t = (uint64)n * j + (uint64)i;
			uint8 tBytes[8];
			for (uint32 k = 0; k < 8; k++)
				tBytes[k] = (uint8)(t >> (56 - k * 8));

			// A ^= t; B = AES-DEC(KEK, A | R[i])
			uint8 in[16];
			for (uint32 k = 0; k < 8; k++)
				in[k] = A[k] ^ tBytes[k];
			memcpy(in + 8, plaintext + (i - 1) * 8, 8);

			aes128_decrypt(kek, in, block);

			memcpy(A, block, 8);
			memcpy(plaintext + (i - 1) * 8, block + 8, 8);
		}
	}

	// Integrity check: A must equal IV = 0xA6A6A6A6A6A6A6A6.
	for (uint32 i = 0; i < 8; i++) {
		if (A[i] != 0xA6)
			return false;
	}
	return true;
}


}	// namespace wpa2_crypto
