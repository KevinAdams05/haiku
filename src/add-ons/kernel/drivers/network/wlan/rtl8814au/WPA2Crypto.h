/*
 * WPA2Crypto.h — kernel-side cryptographic primitives for the
 * in-driver WPA2-PSK 4-way handshake.
 *
 * Standalone implementations: no allocations, no kernel-only
 * dependencies, no globals.  All functions are reentrant.
 *
 * Per IEEE 802.11i and RFC 3174 / RFC 3394.  Primitives are
 * intentionally minimal — encrypt-only AES is exposed since CCMP
 * data-frame encryption happens in the chip's hardware crypto
 * engine; AES decrypt is only here for RFC 3394 key unwrap on
 * the GTK delivered in EAPOL-Key Message 3.
 */

#ifndef _RTL8814AU_WPA2_CRYPTO_H
#define _RTL8814AU_WPA2_CRYPTO_H

#include <SupportDefs.h>


namespace wpa2_crypto {

// SHA-1 hash of `len` bytes at `data` into the 20-byte `digest`.
void sha1(const uint8* data, uint32 len, uint8 digest[20]);

// HMAC-SHA1 per RFC 2104.  Output is the full 20-byte MAC; callers
// that need a 16-byte MIC simply truncate.
void hmac_sha1(const uint8* key, uint32 keyLen,
	const uint8* msg, uint32 msgLen, uint8 mac[20]);

// PRF-X per IEEE 802.11i §8.5.1.1: prf_384 produces 48 bytes (the
// 384-bit pairwise expansion needed for CCMP PTK derivation).
//
//   PRF-384(K, A, B) = HMAC-SHA1(K, A || 0x00 || B || 0)
//                   || HMAC-SHA1(K, A || 0x00 || B || 1)
//                   || HMAC-SHA1(K, A || 0x00 || B || 2)[0..7]
void prf_384(const uint8* key, uint32 keyLen,
	const char* label, uint32 labelLen,
	const uint8* data, uint32 dataLen,
	uint8 output[48]);

// AES-128 ECB block cipher.  16-byte key, 16-byte block.
void aes128_encrypt(const uint8 key[16], const uint8 plaintext[16],
	uint8 ciphertext[16]);
void aes128_decrypt(const uint8 key[16], const uint8 ciphertext[16],
	uint8 plaintext[16]);

// RFC 3394 AES Key Wrap unwrap.  Used to extract the GTK from
// EAPOL-Key Message 3.  `cipherLen` must be 8*n+8 for n >= 1.
// On success, plaintext output is `cipherLen - 8` bytes.
// Returns true if the integrity check (IV match) passed.
bool aes_unwrap(const uint8 kek[16], const uint8* cipher, uint32 cipherLen,
	uint8* plaintext);

}	// namespace wpa2_crypto


#endif	// _RTL8814AU_WPA2_CRYPTO_H
