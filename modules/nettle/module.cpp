#include "module.h"
#include <cryptofuzz/util.h>
#include <nettle/arcfour.h>
#include <nettle/blowfish.h>
#include <nettle/cast128.h>
#include <nettle/ccm.h>
#include <nettle/chacha-poly1305.h>
#include <nettle/chacha.h>
#include <nettle/cmac.h>
#include <nettle/des.h>
#include <nettle/eax.h>
#if defined(HAVE_LIBHOGWEED)
#include <nettle/ecc-curve.h>
#include <nettle/ecc.h>
#include <nettle/ecdsa.h>
#include <nettle/knuth-lfib.h>
#endif
#include <nettle/gcm.h>
#include <nettle/gosthash94.h>
#include <nettle/hkdf.h>
#include <nettle/hmac.h>
#include <nettle/md2.h>
#include <nettle/md4.h>
#include <nettle/md5.h>
#include <nettle/pbkdf2.h>
#include <nettle/ripemd160.h>
#include <nettle/salsa20.h>
#include <nettle/serpent.h>
#include <nettle/sha.h>
#include <nettle/sha3.h>
#include <nettle/streebog.h>
#include <nettle/twofish.h>
#include <nettle/xts.h>

namespace cryptofuzz {
namespace module {

Nettle::Nettle(void) :
    Module("Nettle") { }

namespace Nettle_detail {
    template <class OperationType, class ReturnType, class CTXType>
    class Operation {
        protected:
            CTXType ctx;
        public:
            Operation(void) { }
            ~Operation() { }
            virtual bool runInit(OperationType& op) = 0;
            virtual void runUpdate(util::Multipart& parts) = 0;
            virtual std::vector<uint8_t> runFinalize(void) = 0;
            std::optional<ReturnType> Run(OperationType& op) {
                Datasource ds(op.modifier.GetPtr(), op.modifier.GetSize());
                util::Multipart parts;

                if ( runInit(op) == false ) {
                    return std::nullopt;
                }

                parts = util::ToParts(ds, op.cleartext);

                runUpdate(parts);

                {
                    const auto ret = runFinalize();
                    return ReturnType(ret.data(), ret.size());
                }
            }
    };

    template <class CTXType, size_t DigestSize>
    class Digest : public Operation<operation::Digest, component::Digest, CTXType> {
        private:
            void (*init)(CTXType*);
            void (*update)(CTXType*, size_t, const uint8_t*);
            void (*digest)(CTXType*, size_t, uint8_t*);
        public:
            Digest(
                void (*init)(CTXType*),
                void (*update)(CTXType*, size_t, const uint8_t*),
                void (*digest)(CTXType*, size_t, uint8_t*)
            ) :
                Operation<operation::Digest, component::Digest, CTXType>(),
                init(init),
                update(update),
                digest(digest)
            { }

            bool runInit(operation::Digest& op) override {
                (void)op;
                /* noret */ init(&this->ctx);
                return true;
            }

            void runUpdate(util::Multipart& parts) override {
                for (const auto& part : parts) {
                    /* noret */ update(&this->ctx, part.second, part.first);
                }
            }

            std::vector<uint8_t> runFinalize(void) override {
                std::vector<uint8_t> ret(DigestSize);
                /* noret */ digest(&this->ctx, DigestSize, ret.data());
                return ret;
            }
    };

    template <class CTXType, size_t DigestSize>
    class HMAC : public Operation<operation::HMAC, component::MAC, CTXType> {
        private:
            void (*set_key)(CTXType*, size_t, const uint8_t*);
            void (*update)(CTXType*, size_t, const uint8_t*);
            void (*digest)(CTXType*, size_t, uint8_t*);
        public:
            HMAC(
                void (*set_key)(CTXType*, size_t, const uint8_t*),
                void (*update)(CTXType*, size_t, const uint8_t*),
                void (*digest)(CTXType*, size_t, uint8_t*)
            ) :
                Operation<operation::HMAC, component::MAC, CTXType>(),
                set_key(set_key),
                update(update),
                digest(digest)
            { }

            bool runInit(operation::HMAC& op) override {
                /* noret */ set_key(&this->ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
                return true;
            }

            void runUpdate(util::Multipart& parts) override {
                for (const auto& part : parts) {
                    /* noret */ update(&this->ctx, part.second, part.first);
                }
            }

            std::vector<uint8_t> runFinalize(void) override {
                std::vector<uint8_t> ret(DigestSize);
                /* noret */ digest(&this->ctx, DigestSize, ret.data());
                return ret;
            }
    };

    template <class CTXType, size_t DigestSize, size_t KeySize>
    class CMAC : public Operation<operation::CMAC, component::MAC, CTXType> {
        private:
            void (*set_key)(CTXType*, const uint8_t*);
            void (*update)(CTXType*, size_t, const uint8_t*);
            void (*digest)(CTXType*, size_t, uint8_t*);
        public:
            CMAC(
                void (*set_key)(CTXType*, const uint8_t*),
                void (*update)(CTXType*, size_t, const uint8_t*),
                void (*digest)(CTXType*, size_t, uint8_t*)
            ) :
                Operation<operation::CMAC, component::MAC, CTXType>(),
                set_key(set_key),
                update(update),
                digest(digest)
            { }

            bool runInit(operation::CMAC& op) override {
                bool ret = false;

                CF_CHECK_EQ(op.cipher.key.GetSize(), KeySize);

                /* noret */ set_key(&this->ctx, op.cipher.key.GetPtr());

                ret = true;

end:
                return ret;
            }

            void runUpdate(util::Multipart& parts) override {
                for (const auto& part : parts) {
                    /* noret */ update(&this->ctx, part.second, part.first);
                }
            }

            std::vector<uint8_t> runFinalize(void) override {
                std::vector<uint8_t> ret(DigestSize);
                /* noret */ digest(&this->ctx, DigestSize, ret.data());
                return ret;
            }
    };

    Digest<md2_ctx, MD2_DIGEST_SIZE> md2(md2_init, md2_update, md2_digest);
    Digest<md4_ctx, MD4_DIGEST_SIZE> md4(md4_init, md4_update, md4_digest);
    Digest<md5_ctx, MD5_DIGEST_SIZE> md5(md5_init, md5_update, md5_digest);
    Digest<ripemd160_ctx, RIPEMD160_DIGEST_SIZE> ripemd160(ripemd160_init, ripemd160_update, ripemd160_digest);
    Digest<sha1_ctx, SHA1_DIGEST_SIZE> sha1(sha1_init, sha1_update, sha1_digest);
    Digest<sha224_ctx, SHA224_DIGEST_SIZE> sha224(sha224_init, sha224_update, sha224_digest);
    Digest<sha256_ctx, SHA256_DIGEST_SIZE> sha256(sha256_init, sha256_update, sha256_digest);
    Digest<sha384_ctx, SHA384_DIGEST_SIZE> sha384(sha384_init, sha384_update, sha384_digest);
    Digest<sha512_ctx, SHA512_DIGEST_SIZE> sha512(sha512_init, sha512_update, sha512_digest);
    Digest<sha512_224_ctx, SHA512_224_DIGEST_SIZE> sha512_224(sha512_224_init, sha512_224_update, sha512_224_digest);
    Digest<sha512_256_ctx, SHA512_256_DIGEST_SIZE> sha512_256(sha512_256_init, sha512_256_update, sha512_256_digest);
    Digest<gosthash94_ctx, GOSTHASH94_DIGEST_SIZE> gosthash94(gosthash94_init, gosthash94_update, gosthash94_digest);
    Digest<sha3_224_ctx, SHA3_224_DIGEST_SIZE> sha3_224(sha3_224_init, sha3_224_update, sha3_224_digest);
    Digest<sha3_256_ctx, SHA3_256_DIGEST_SIZE> sha3_256(sha3_256_init, sha3_256_update, sha3_256_digest);
    Digest<sha3_384_ctx, SHA3_384_DIGEST_SIZE> sha3_384(sha3_384_init, sha3_384_update, sha3_384_digest);
    Digest<sha3_512_ctx, SHA3_512_DIGEST_SIZE> sha3_512(sha3_512_init, sha3_512_update, sha3_512_digest);
    Digest<streebog256_ctx, STREEBOG256_DIGEST_SIZE> streebog_256(streebog256_init, streebog256_update, streebog256_digest);
    Digest<streebog512_ctx, STREEBOG512_DIGEST_SIZE> streebog_512(streebog512_init, streebog512_update, streebog512_digest);

    HMAC<hmac_md5_ctx, MD5_DIGEST_SIZE> hmac_md5(hmac_md5_set_key, hmac_md5_update, hmac_md5_digest);
    HMAC<hmac_ripemd160_ctx, RIPEMD160_DIGEST_SIZE> hmac_ripemd160(hmac_ripemd160_set_key, hmac_ripemd160_update, hmac_ripemd160_digest);
    HMAC<hmac_sha1_ctx, SHA1_DIGEST_SIZE> hmac_sha1(hmac_sha1_set_key, hmac_sha1_update, hmac_sha1_digest);
    HMAC<hmac_sha224_ctx, SHA224_DIGEST_SIZE> hmac_sha224(hmac_sha224_set_key, hmac_sha224_update, hmac_sha224_digest);
    HMAC<hmac_sha256_ctx, SHA256_DIGEST_SIZE> hmac_sha256(hmac_sha256_set_key, hmac_sha256_update, hmac_sha256_digest);
    HMAC<hmac_sha512_ctx, SHA512_DIGEST_SIZE> hmac_sha512(hmac_sha512_set_key, hmac_sha512_update, hmac_sha512_digest);
    HMAC<hmac_streebog256_ctx, SHA256_DIGEST_SIZE> hmac_streebog256(hmac_streebog256_set_key, hmac_streebog256_update, hmac_streebog256_digest);
    HMAC<hmac_streebog512_ctx, SHA512_DIGEST_SIZE> hmac_streebog512(hmac_streebog512_set_key, hmac_streebog512_update, hmac_streebog512_digest);

    CMAC<cmac_aes128_ctx, CMAC128_DIGEST_SIZE, 16> cmac_aes128(cmac_aes128_set_key, cmac_aes128_update, cmac_aes128_digest);
    CMAC<cmac_aes256_ctx, CMAC128_DIGEST_SIZE, 32> cmac_aes256(cmac_aes256_set_key, cmac_aes256_update, cmac_aes256_digest);

} /* namespace Nettle_detail */

std::optional<component::Digest> Nettle::OpDigest(operation::Digest& op) {
    std::optional<component::Digest> ret = std::nullopt;

    switch ( op.digestType.Get() ) {
        case CF_DIGEST("MD2"):
            ret = Nettle_detail::md2.Run(op);
            break;
        case CF_DIGEST("MD4"):
            ret = Nettle_detail::md4.Run(op);
            break;
        case CF_DIGEST("MD5"):
            ret = Nettle_detail::md5.Run(op);
            break;
        case CF_DIGEST("RIPEMD160"):
            ret = Nettle_detail::ripemd160.Run(op);
            break;
        case CF_DIGEST("SHA1"):
            ret = Nettle_detail::sha1.Run(op);
            break;
        case CF_DIGEST("SHA224"):
            ret = Nettle_detail::sha224.Run(op);
            break;
        case CF_DIGEST("SHA256"):
            ret = Nettle_detail::sha256.Run(op);
            break;
        case CF_DIGEST("SHA384"):
            ret = Nettle_detail::sha384.Run(op);
            break;
        case CF_DIGEST("SHA512"):
            ret = Nettle_detail::sha512.Run(op);
            break;
        case CF_DIGEST("SHA512-224"):
            ret = Nettle_detail::sha512_224.Run(op);
            break;
        case CF_DIGEST("SHA512-256"):
            ret = Nettle_detail::sha512_256.Run(op);
            break;
        case CF_DIGEST("SHA3-224"):
            ret = Nettle_detail::sha3_224.Run(op);
            break;
        case CF_DIGEST("SHA3-256"):
            ret = Nettle_detail::sha3_256.Run(op);
            break;
        case CF_DIGEST("SHA3-384"):
            ret = Nettle_detail::sha3_384.Run(op);
            break;
        case CF_DIGEST("SHA3-512"):
            ret = Nettle_detail::sha3_512.Run(op);
            break;
        case CF_DIGEST("GOST-R-34.11-94-NO-CRYPTOPRO"):
            ret = Nettle_detail::gosthash94.Run(op);
            break;
        case CF_DIGEST("STREEBOG-256"):
            ret = Nettle_detail::streebog_256.Run(op);
            break;
        case CF_DIGEST("STREEBOG-512"):
            ret = Nettle_detail::streebog_512.Run(op);
            break;
    }

    return ret;
}

std::optional<component::MAC> Nettle::OpHMAC(operation::HMAC& op) {
    std::optional<component::MAC> ret = std::nullopt;

    switch ( op.digestType.Get() ) {
        case CF_DIGEST("MD5"):
            ret = Nettle_detail::hmac_md5.Run(op);
            break;
        case CF_DIGEST("RIPEMD160"):
            ret = Nettle_detail::hmac_ripemd160.Run(op);
            break;
        case CF_DIGEST("SHA1"):
            ret = Nettle_detail::hmac_sha1.Run(op);
            break;
        case CF_DIGEST("SHA256"):
            ret = Nettle_detail::hmac_sha256.Run(op);
            break;
        case CF_DIGEST("SHA512"):
            ret = Nettle_detail::hmac_sha512.Run(op);
            break;
        case CF_DIGEST("STREEBOG-256"):
            ret = Nettle_detail::hmac_streebog256.Run(op);
            break;
        case CF_DIGEST("STREEBOG-512"):
            ret = Nettle_detail::hmac_streebog512.Run(op);
            break;
    }

    return ret;
}

std::optional<component::MAC> Nettle::OpCMAC(operation::CMAC& op) {
    std::optional<component::MAC> ret = std::nullopt;

    switch ( op.cipher.cipherType.Get() ) {
        case CF_CIPHER("AES_128_CBC"):
            ret = Nettle_detail::cmac_aes128.Run(op);
            break;
        case CF_CIPHER("AES_256_CBC"):
            ret = Nettle_detail::cmac_aes256.Run(op);
            break;
    }

    return ret;
}

namespace Nettle_detail {
    bool Salsa20_CheckParts(const util::Multipart& parts) {
        if ( parts.empty() ) {
            return true;
        }

        /* all but the last call must use a length that is a multiple of SALSA20_BLOCK_SIZE */
        if ( parts.back().second > 0 && (parts.back().second % SALSA20_BLOCK_SIZE) == 0 ) {
            return true;
        }

        return false;
    }

    std::optional<Buffer> Salsa20Crypt(const Buffer& in, const component::SymmetricCipher& cipher, const size_t keySize, const bool rounds20) {
        std::optional<Buffer> ret = std::nullopt;

        if ( cipher.iv.GetSize() != SALSA20_NONCE_SIZE ) {
            return ret;
        }

        if ( cipher.key.GetSize() != keySize ) {
            return ret;
        }

        uint8_t* out = util::malloc(in.GetSize());
        struct salsa20_ctx ctx;
        size_t outIdx = 0;

        auto parts = util::ToEqualParts(in, SALSA20_BLOCK_SIZE);

        /* noret */ salsa20_set_key(&ctx, cipher.key.GetSize(), cipher.key.GetPtr());
        /* noret */ salsa20_set_iv(&ctx, cipher.iv.GetPtr());

        for (const auto& part : parts) {
            if ( rounds20 == true ) {
                /* noret */ salsa20_crypt(&ctx, part.second, out + outIdx, part.first);
            } else {
                /* noret */ salsa20r12_crypt(&ctx, part.second, out + outIdx, part.first);
            }
            outIdx += part.second;
        }

        ret = Buffer(out, in.GetSize());

//end:
        util::free(out);
        return ret;
    }
}

std::optional<component::Ciphertext> Nettle::OpSymmetricEncrypt(operation::SymmetricEncrypt& op) {
    std::optional<component::Ciphertext> ret = std::nullopt;
    Datasource ds(op.modifier.GetPtr(), op.modifier.GetSize());

    uint8_t* out = nullptr;
    uint8_t* outTag = nullptr;

    switch ( op.cipher.cipherType.Get() ) {
        case CF_CIPHER("AES_128_GCM"):
        {
            struct gcm_aes128_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 128 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            CF_CHECK_LTE(*op.tagSize, GCM_DIGEST_SIZE);

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ gcm_aes128_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_aes128_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_aes128_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_aes128_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ gcm_aes128_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("AES_192_GCM"):
        {
            struct gcm_aes192_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 192 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            CF_CHECK_LTE(*op.tagSize, GCM_DIGEST_SIZE);

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ gcm_aes192_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_aes192_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_aes192_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_aes192_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ gcm_aes192_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("AES_256_GCM"):
        {
            struct gcm_aes256_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 256 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            CF_CHECK_LTE(*op.tagSize, GCM_DIGEST_SIZE);

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ gcm_aes256_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_aes256_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_aes256_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_aes256_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ gcm_aes256_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("AES_128_EAX"):
        {
            struct eax_aes128_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 128 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            CF_CHECK_LTE(*op.tagSize, EAX_DIGEST_SIZE);
            CF_CHECK_GT(*op.tagSize, 0); /* XXX crashes without this check. This is probably a bug in Nettle */

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ eax_aes128_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ eax_aes128_set_nonce(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ eax_aes128_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ eax_aes128_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ eax_aes128_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("AES_128_CCM"):
        {
            struct ccm_aes128_ctx ctx;

            CF_CHECK_GTE(op.cipher.iv.GetSize(), 7);
            CF_CHECK_LTE(op.cipher.iv.GetSize(), 13);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 128 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            {
                static const std::vector<size_t> validTagSizes = {4, 6, 8, 10, 12, 14, 16};
                if ( std::find(validTagSizes.begin(), validTagSizes.end(), *op.tagSize) == validTagSizes.end() ) {
                    goto end;
                }
            }

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ ccm_aes128_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ ccm_aes128_set_nonce(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr(), op.aad == std::nullopt ? 0 : op.aad->GetSize(), op.cleartext.GetSize(), *op.tagSize);
            if ( op.aad != std::nullopt ) {
                /* noret */ ccm_aes128_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ ccm_aes128_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ ccm_aes128_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("AES_192_CCM"):
        {
            struct ccm_aes192_ctx ctx;

            CF_CHECK_GTE(op.cipher.iv.GetSize(), 7);
            CF_CHECK_LTE(op.cipher.iv.GetSize(), 13);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 192 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            {
                static const std::vector<size_t> validTagSizes = {4, 6, 8, 10, 12, 14, 16};
                if ( std::find(validTagSizes.begin(), validTagSizes.end(), *op.tagSize) == validTagSizes.end() ) {
                    goto end;
                }
            }

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ ccm_aes192_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ ccm_aes192_set_nonce(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr(), op.aad == std::nullopt ? 0 : op.aad->GetSize(), op.cleartext.GetSize(), *op.tagSize);
            if ( op.aad != std::nullopt ) {
                /* noret */ ccm_aes192_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ ccm_aes192_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ ccm_aes192_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("AES_256_CCM"):
        {
            struct ccm_aes256_ctx ctx;

            CF_CHECK_GTE(op.cipher.iv.GetSize(), 7);
            CF_CHECK_LTE(op.cipher.iv.GetSize(), 13);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 256 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            {
                static const std::vector<size_t> validTagSizes = {4, 6, 8, 10, 12, 14, 16};
                if ( std::find(validTagSizes.begin(), validTagSizes.end(), *op.tagSize) == validTagSizes.end() ) {
                    goto end;
                }
            }

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ ccm_aes256_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ ccm_aes256_set_nonce(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr(), op.aad == std::nullopt ? 0 : op.aad->GetSize(), op.cleartext.GetSize(), *op.tagSize);
            if ( op.aad != std::nullopt ) {
                /* noret */ ccm_aes256_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ ccm_aes256_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ ccm_aes256_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;


        case CF_CIPHER("CHACHA20"):
        {
            struct chacha_ctx ctx;
            CF_CHECK_EQ(op.cipher.iv.GetSize(), CHACHA_NONCE_SIZE);
            CF_CHECK_EQ(op.cipher.key.GetSize(), CHACHA_KEY_SIZE);
            out = util::malloc(op.cleartext.GetSize());

            /* noret */ chacha_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ chacha_set_nonce(&ctx, op.cipher.iv.GetPtr());
            /* noret */ chacha_crypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;

        case CF_CIPHER("CHACHA20_POLY1305"):
        {
            struct chacha_poly1305_ctx ctx;

            CF_CHECK_EQ(op.cipher.iv.GetSize(), CHACHA_POLY1305_NONCE_SIZE);
            CF_CHECK_EQ(op.cipher.key.GetSize(), CHACHA_POLY1305_KEY_SIZE);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            CF_CHECK_LTE(*op.tagSize, CHACHA_POLY1305_DIGEST_SIZE);

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ chacha_poly1305_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ chacha_poly1305_set_nonce(&ctx, op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ chacha_poly1305_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ chacha_poly1305_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ chacha_poly1305_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("AES_128_XTS"):
        {
            struct xts_aes128_key ctx;
            CF_CHECK_EQ(op.cipher.key.GetSize(), 256 / 8);
            CF_CHECK_GT(op.cleartext.GetSize(), 0);
            CF_CHECK_EQ(op.cleartext.GetSize() % 16, 0);
            CF_CHECK_EQ(op.cipher.iv.GetSize(), XTS_BLOCK_SIZE);

            out = util::malloc(op.cleartext.GetSize());

            /* noret */ xts_aes128_set_encrypt_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ xts_aes128_encrypt_message(&ctx, op.cipher.iv.GetPtr(), op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_256_XTS"):
        {
            struct xts_aes256_key ctx;
            CF_CHECK_EQ(op.cipher.key.GetSize(), 512 / 8);
            CF_CHECK_GT(op.cleartext.GetSize(), 0);
            CF_CHECK_EQ(op.cleartext.GetSize() % 16, 0);
            CF_CHECK_EQ(op.cipher.iv.GetSize(), XTS_BLOCK_SIZE);

            out = util::malloc(op.cleartext.GetSize());

            /* noret */ xts_aes256_set_encrypt_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ xts_aes256_encrypt_message(&ctx, op.cipher.iv.GetPtr(), op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;

        case CF_CIPHER("RC4"):
        {
            struct arcfour_ctx ctx;

            CF_CHECK_GTE(op.cipher.key.GetSize(), ARCFOUR_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), ARCFOUR_MAX_KEY_SIZE);

            out = util::malloc(op.cleartext.GetSize());

            /* noret */ arcfour_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ arcfour_crypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;

        case CF_CIPHER("CAMELLIA_128_GCM"):
        {
            struct gcm_camellia128_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 128 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            CF_CHECK_LTE(*op.tagSize, GCM_DIGEST_SIZE);

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ gcm_camellia128_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_camellia128_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_camellia128_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_camellia128_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ gcm_camellia128_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("CAMELLIA_256_GCM"):
        {
            struct gcm_camellia256_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 256 / 8);
            CF_CHECK_NE(op.tagSize, std::nullopt);
            CF_CHECK_LTE(*op.tagSize, GCM_DIGEST_SIZE);

            out = util::malloc(op.cleartext.GetSize());
            outTag = util::malloc(*op.tagSize);

            /* noret */ gcm_camellia256_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_camellia256_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_camellia256_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_camellia256_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());
            /* noret */ gcm_camellia256_digest(&ctx, *op.tagSize, outTag);

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()), Buffer(outTag, *op.tagSize));
        }
        break;

        case CF_CIPHER("SALSA20_128"):
        {
            ret = Nettle_detail::Salsa20Crypt(op.cleartext, op.cipher, SALSA20_128_KEY_SIZE, true);
        }
        break;

        case CF_CIPHER("SALSA20_12_128"):
        {
            ret = Nettle_detail::Salsa20Crypt(op.cleartext, op.cipher, SALSA20_128_KEY_SIZE, false);
        }
        break;

        case CF_CIPHER("SALSA20_256"):
        {
            ret = Nettle_detail::Salsa20Crypt(op.cleartext, op.cipher, SALSA20_256_KEY_SIZE, true);
        }
        break;

        case CF_CIPHER("SALSA20_12_256"):
        {
            ret = Nettle_detail::Salsa20Crypt(op.cleartext, op.cipher, SALSA20_256_KEY_SIZE, false);
        }
        break;

        case CF_CIPHER("DES_ECB"):
        {
            struct des_ctx ctx;

            CF_CHECK_EQ(op.cleartext.GetSize() % DES_BLOCK_SIZE, 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), DES_KEY_SIZE);

            out = util::malloc(op.cleartext.GetSize());

            /* ignore return value */ des_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ des_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;

        case CF_CIPHER("TWOFISH"):
        {
            struct twofish_ctx ctx;

            CF_CHECK_EQ(op.cleartext.GetSize() % TWOFISH_BLOCK_SIZE, 0);
            CF_CHECK_GTE(op.cipher.key.GetSize(), TWOFISH_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), TWOFISH_MAX_KEY_SIZE);

            out = util::malloc(op.cleartext.GetSize());

            /* ignore return value */ twofish_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ twofish_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;

        case CF_CIPHER("SERPENT"):
        {
            struct serpent_ctx ctx;

            CF_CHECK_EQ(op.cleartext.GetSize() % SERPENT_BLOCK_SIZE, 0);
            CF_CHECK_GTE(op.cipher.key.GetSize(), SERPENT_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), SERPENT_MAX_KEY_SIZE);

            out = util::malloc(op.cleartext.GetSize());

            /* ignore return value */ serpent_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ serpent_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;

        case CF_CIPHER("BLOWFISH_ECB"):
        {
            struct blowfish_ctx ctx;

            CF_CHECK_EQ(op.cleartext.GetSize() % BLOWFISH_BLOCK_SIZE, 0);
            CF_CHECK_GTE(op.cipher.key.GetSize(), BLOWFISH_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), BLOWFISH_MAX_KEY_SIZE);

            out = util::malloc(op.cleartext.GetSize());

            /* ignore return value */ blowfish_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ blowfish_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;

        case CF_CIPHER("CAST5_ECB"):
        {
            struct cast128_ctx ctx;

            CF_CHECK_EQ(op.cleartext.GetSize() % CAST128_BLOCK_SIZE, 0);
            CF_CHECK_GTE(op.cipher.key.GetSize(), CAST5_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), CAST5_MAX_KEY_SIZE);

            out = util::malloc(op.cleartext.GetSize());

            /* ignore return value */ cast5_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ cast128_encrypt(&ctx, op.cleartext.GetSize(), out, op.cleartext.GetPtr());

            ret = component::Ciphertext(Buffer(out, op.cleartext.GetSize()));
        }
        break;
    }

end:
    util::free(out);
    util::free(outTag);

    return ret;
}


std::optional<component::Cleartext> Nettle::OpSymmetricDecrypt(operation::SymmetricDecrypt& op) {
    std::optional<component::Cleartext> ret = std::nullopt;
    Datasource ds(op.modifier.GetPtr(), op.modifier.GetSize());

    uint8_t* out = nullptr;
    uint8_t* outTag = nullptr;

    switch ( op.cipher.cipherType.Get() ) {
        case CF_CIPHER("AES_128_GCM"):
        {
            struct gcm_aes128_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 128 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            CF_CHECK_LTE(op.tag->GetSize(), GCM_DIGEST_SIZE);

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ gcm_aes128_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_aes128_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_aes128_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_aes128_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ gcm_aes128_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_192_GCM"):
        {
            struct gcm_aes192_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 192 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            CF_CHECK_LTE(op.tag->GetSize(), GCM_DIGEST_SIZE);

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ gcm_aes192_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_aes192_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_aes192_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_aes192_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ gcm_aes192_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_256_GCM"):
        {
            struct gcm_aes256_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 256 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            CF_CHECK_LTE(op.tag->GetSize(), GCM_DIGEST_SIZE);

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ gcm_aes256_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_aes256_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_aes256_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_aes256_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ gcm_aes256_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_128_EAX"):
        {
            struct eax_aes128_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 128 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            CF_CHECK_LTE(op.tag->GetSize(), EAX_DIGEST_SIZE);
            CF_CHECK_GT(op.tag->GetSize(), 0); /* XXX crashes without this check. This is probably a bug in Nettle */

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ eax_aes128_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ eax_aes128_set_nonce(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ eax_aes128_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ eax_aes128_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ eax_aes128_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_128_CCM"):
        {
            struct ccm_aes128_ctx ctx;

            CF_CHECK_GTE(op.cipher.iv.GetSize(), 7);
            CF_CHECK_LTE(op.cipher.iv.GetSize(), 13);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 128 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            {
                static const std::vector<size_t> validTagSizes = {4, 6, 8, 10, 12, 14, 16};
                if ( std::find(validTagSizes.begin(), validTagSizes.end(), op.tag->GetSize()) == validTagSizes.end() ) {
                    goto end;
                }
            }

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ ccm_aes128_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ ccm_aes128_set_nonce(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr(), op.aad == std::nullopt ? 0 : op.aad->GetSize(), op.ciphertext.GetSize(), op.tag->GetSize());
            if ( op.aad != std::nullopt ) {
                /* noret */ ccm_aes128_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ ccm_aes128_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ ccm_aes128_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_192_CCM"):
        {
            struct ccm_aes192_ctx ctx;

            CF_CHECK_GTE(op.cipher.iv.GetSize(), 7);
            CF_CHECK_LTE(op.cipher.iv.GetSize(), 13);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 192 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            {
                static const std::vector<size_t> validTagSizes = {4, 6, 8, 10, 12, 14, 16};
                if ( std::find(validTagSizes.begin(), validTagSizes.end(), op.tag->GetSize()) == validTagSizes.end() ) {
                    goto end;
                }
            }

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ ccm_aes192_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ ccm_aes192_set_nonce(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr(), op.aad == std::nullopt ? 0 : op.aad->GetSize(), op.ciphertext.GetSize(), op.tag->GetSize());
            if ( op.aad != std::nullopt ) {
                /* noret */ ccm_aes192_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ ccm_aes192_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ ccm_aes192_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_256_CCM"):
        {
            struct ccm_aes256_ctx ctx;

            CF_CHECK_GTE(op.cipher.iv.GetSize(), 7);
            CF_CHECK_LTE(op.cipher.iv.GetSize(), 13);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 256 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            {
                static const std::vector<size_t> validTagSizes = {4, 6, 8, 10, 12, 14, 16};
                if ( std::find(validTagSizes.begin(), validTagSizes.end(), op.tag->GetSize()) == validTagSizes.end() ) {
                    goto end;
                }
            }

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ ccm_aes256_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ ccm_aes256_set_nonce(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr(), op.aad == std::nullopt ? 0 : op.aad->GetSize(), op.ciphertext.GetSize(), op.tag->GetSize());
            if ( op.aad != std::nullopt ) {
                /* noret */ ccm_aes256_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ ccm_aes256_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ ccm_aes256_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("CHACHA20"):
        {
            struct chacha_ctx ctx;
            CF_CHECK_EQ(op.cipher.iv.GetSize(), CHACHA_NONCE_SIZE);
            CF_CHECK_EQ(op.cipher.key.GetSize(), CHACHA_KEY_SIZE);
            out = util::malloc(op.ciphertext.GetSize());

            /* noret */ chacha_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ chacha_set_nonce(&ctx, op.cipher.iv.GetPtr());
            /* noret */ chacha_crypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("CHACHA20_POLY1305"):
        {
            struct chacha_poly1305_ctx ctx;

            CF_CHECK_EQ(op.cipher.iv.GetSize(), CHACHA_POLY1305_NONCE_SIZE);
            CF_CHECK_EQ(op.cipher.key.GetSize(), CHACHA_POLY1305_KEY_SIZE);
            CF_CHECK_NE(op.tag, std::nullopt);
            CF_CHECK_LTE(op.tag->GetSize(), CHACHA_POLY1305_DIGEST_SIZE);

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ chacha_poly1305_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ chacha_poly1305_set_nonce(&ctx, op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ chacha_poly1305_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ chacha_poly1305_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ chacha_poly1305_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_128_XTS"):
        {
            struct xts_aes128_key ctx;
            CF_CHECK_EQ(op.cipher.key.GetSize(), 256 / 8);
            CF_CHECK_GT(op.ciphertext.GetSize(), 0);
            CF_CHECK_EQ(op.ciphertext.GetSize() % 16, 0);
            CF_CHECK_EQ(op.cipher.iv.GetSize(), XTS_BLOCK_SIZE);

            out = util::malloc(op.ciphertext.GetSize());

            /* noret */ xts_aes128_set_decrypt_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ xts_aes128_decrypt_message(&ctx, op.cipher.iv.GetPtr(), op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("AES_256_XTS"):
        {
            struct xts_aes256_key ctx;
            CF_CHECK_EQ(op.cipher.key.GetSize(), 512 / 8);
            CF_CHECK_GT(op.ciphertext.GetSize(), 0);
            CF_CHECK_EQ(op.ciphertext.GetSize() % 16, 0);
            CF_CHECK_EQ(op.cipher.iv.GetSize(), XTS_BLOCK_SIZE);

            out = util::malloc(op.ciphertext.GetSize());

            /* noret */ xts_aes256_set_decrypt_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ xts_aes256_decrypt_message(&ctx, op.cipher.iv.GetPtr(), op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("RC4"):
        {
            struct arcfour_ctx ctx;

            CF_CHECK_GTE(op.cipher.key.GetSize(), ARCFOUR_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), ARCFOUR_MAX_KEY_SIZE);

            out = util::malloc(op.ciphertext.GetSize());

            /* noret */ arcfour_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ arcfour_crypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("CAMELLIA_128_GCM"):
        {
            struct gcm_camellia128_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 128 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            CF_CHECK_LTE(op.tag->GetSize(), GCM_DIGEST_SIZE);

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ gcm_camellia128_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_camellia128_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_camellia128_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_camellia128_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ gcm_camellia128_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("CAMELLIA_256_GCM"):
        {
            struct gcm_camellia256_ctx ctx;

            CF_CHECK_NE(op.cipher.iv.GetSize(), 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), 256 / 8);
            CF_CHECK_NE(op.tag, std::nullopt);
            CF_CHECK_LTE(op.tag->GetSize(), GCM_DIGEST_SIZE);

            out = util::malloc(op.ciphertext.GetSize());
            outTag = util::malloc(op.tag->GetSize());

            /* noret */ gcm_camellia256_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ gcm_camellia256_set_iv(&ctx, op.cipher.iv.GetSize(), op.cipher.iv.GetPtr());
            if ( op.aad != std::nullopt ) {
                /* noret */ gcm_camellia256_update(&ctx, op.aad->GetSize(), op.aad->GetPtr());
            }
            /* noret */ gcm_camellia256_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());
            /* noret */ gcm_camellia256_digest(&ctx, op.tag->GetSize(), outTag);

            CF_CHECK_EQ(memcmp(op.tag->GetPtr(), outTag, op.tag->GetSize()), 0);

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("SALSA20_128"):
        {
            ret = Nettle_detail::Salsa20Crypt(op.ciphertext, op.cipher, SALSA20_128_KEY_SIZE, true);
        }
        break;

        case CF_CIPHER("SALSA20_12_128"):
        {
            ret = Nettle_detail::Salsa20Crypt(op.ciphertext, op.cipher, SALSA20_128_KEY_SIZE, false);
        }
        break;

        case CF_CIPHER("SALSA20_256"):
        {
            ret = Nettle_detail::Salsa20Crypt(op.ciphertext, op.cipher, SALSA20_256_KEY_SIZE, true);
        }
        break;

        case CF_CIPHER("SALSA20_12_256"):
        {
            ret = Nettle_detail::Salsa20Crypt(op.ciphertext, op.cipher, SALSA20_256_KEY_SIZE, false);
        }
        break;

        case CF_CIPHER("DES_ECB"):
        {
            struct des_ctx ctx;

            CF_CHECK_EQ(op.ciphertext.GetSize() % DES_BLOCK_SIZE, 0);
            CF_CHECK_EQ(op.cipher.key.GetSize(), DES_KEY_SIZE);

            out = util::malloc(op.ciphertext.GetSize());

            /* ignore return value */ des_set_key(&ctx, op.cipher.key.GetPtr());
            /* noret */ des_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("TWOFISH"):
        {
            struct twofish_ctx ctx;

            CF_CHECK_EQ(op.ciphertext.GetSize() % TWOFISH_BLOCK_SIZE, 0);
            CF_CHECK_GTE(op.cipher.key.GetSize(), TWOFISH_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), TWOFISH_MAX_KEY_SIZE);

            out = util::malloc(op.ciphertext.GetSize());

            /* ignore return value */ twofish_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ twofish_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("SERPENT"):
        {
            struct serpent_ctx ctx;

            CF_CHECK_EQ(op.ciphertext.GetSize() % SERPENT_BLOCK_SIZE, 0);
            CF_CHECK_GTE(op.cipher.key.GetSize(), SERPENT_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), SERPENT_MAX_KEY_SIZE);

            out = util::malloc(op.ciphertext.GetSize());

            /* ignore return value */ serpent_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ serpent_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("BLOWFISH_ECB"):
        {
            struct blowfish_ctx ctx;

            CF_CHECK_EQ(op.ciphertext.GetSize() % BLOWFISH_BLOCK_SIZE, 0);
            CF_CHECK_GTE(op.cipher.key.GetSize(), BLOWFISH_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), BLOWFISH_MAX_KEY_SIZE);

            out = util::malloc(op.ciphertext.GetSize());

            /* ignore return value */ blowfish_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ blowfish_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;

        case CF_CIPHER("CAST5_ECB"):
        {
            struct cast128_ctx ctx;

            CF_CHECK_EQ(op.ciphertext.GetSize() % CAST128_BLOCK_SIZE, 0);
            CF_CHECK_GTE(op.cipher.key.GetSize(), CAST5_MIN_KEY_SIZE);
            CF_CHECK_LTE(op.cipher.key.GetSize(), CAST5_MAX_KEY_SIZE);

            out = util::malloc(op.ciphertext.GetSize());

            /* ignore return value */ cast5_set_key(&ctx, op.cipher.key.GetSize(), op.cipher.key.GetPtr());
            /* noret */ cast128_decrypt(&ctx, op.ciphertext.GetSize(), out, op.ciphertext.GetPtr());

            ret = component::Cleartext(Buffer(out, op.ciphertext.GetSize()));
        }
        break;
    }

end:
    util::free(out);
    util::free(outTag);

    return ret;
}

namespace Nettle_detail {
    template <class CTXType, size_t DigestSize>
    std::optional<component::Key> HKDF(operation::KDF_HKDF& op, void* set_key, void* update, void* digest) {
        typedef void (*set_key_t)(CTXType*, size_t, const uint8_t*);
        std::optional<component::Key> ret = std::nullopt;

        uint8_t* out = util::malloc(op.keySize);

        CTXType ctx;
        uint8_t prk[DigestSize];

        CF_CHECK_LTE(op.keySize, 255 * DigestSize);

        ((set_key_t)set_key)(&ctx, op.salt.GetSize(), op.salt.GetPtr());
        hkdf_extract(&ctx,
                (nettle_hash_update_func*)update,
                (nettle_hash_digest_func*)digest,
                DigestSize,
                op.password.GetSize(), op.password.GetPtr(), prk);
        ((set_key_t)set_key)(&ctx, DigestSize, prk);
        hkdf_expand(&ctx,
                (nettle_hash_update_func*)update,
                (nettle_hash_digest_func*)digest,
                DigestSize,
                op.info.GetSize(),
                op.info.GetPtr(),
                op.keySize,
                out);
        ret = component::Key(out, op.keySize);
end:
        util::free(out);
        return ret;
    }
}

std::optional<component::Key> Nettle::OpKDF_HKDF(operation::KDF_HKDF& op) {
    std::optional<component::Key> ret = std::nullopt;

    switch ( op.digestType.Get() ) {
        case CF_DIGEST("MD5"):
            ret = Nettle_detail::HKDF<hmac_md5_ctx, MD5_DIGEST_SIZE>(op, (void*)hmac_md5_set_key, (void*)hmac_md5_update, (void*)hmac_md5_digest);
            break;
        case CF_DIGEST("RIPEMD160"):
            ret = Nettle_detail::HKDF<hmac_ripemd160_ctx, RIPEMD160_DIGEST_SIZE>(op, (void*)hmac_ripemd160_set_key, (void*)hmac_ripemd160_update, (void*)hmac_ripemd160_digest);
            break;
        case CF_DIGEST("SHA1"):
            ret = Nettle_detail::HKDF<hmac_sha1_ctx, SHA1_DIGEST_SIZE>(op, (void*)hmac_sha1_set_key, (void*)hmac_sha1_update, (void*)hmac_sha1_digest);
            break;
        case CF_DIGEST("SHA224"):
            ret = Nettle_detail::HKDF<hmac_sha224_ctx, SHA224_DIGEST_SIZE>(op, (void*)hmac_sha224_set_key, (void*)hmac_sha224_update, (void*)hmac_sha224_digest);
            break;
        case CF_DIGEST("SHA256"):
            ret = Nettle_detail::HKDF<hmac_sha256_ctx, SHA256_DIGEST_SIZE>(op, (void*)hmac_sha256_set_key, (void*)hmac_sha256_update, (void*)hmac_sha256_digest);
            break;
        case CF_DIGEST("SHA512"):
            ret = Nettle_detail::HKDF<hmac_sha512_ctx, SHA512_DIGEST_SIZE>(op, (void*)hmac_sha512_set_key, (void*)hmac_sha512_update, (void*)hmac_sha512_digest);
            break;
        case CF_DIGEST("STREEBOG-256"):
            ret = Nettle_detail::HKDF<hmac_streebog256_ctx, SHA256_DIGEST_SIZE>(op, (void*)hmac_streebog256_set_key, (void*)hmac_streebog256_update, (void*)hmac_streebog256_digest);
            break;
        case CF_DIGEST("STREEBOG-512"):
            ret = Nettle_detail::HKDF<hmac_streebog512_ctx, SHA512_DIGEST_SIZE>(op, (void*)hmac_streebog512_set_key, (void*)hmac_streebog512_update, (void*)hmac_streebog512_digest);
            break;
    }

    return ret;
}

std::optional<component::Key> Nettle::OpKDF_PBKDF2(operation::KDF_PBKDF2& op) {
    std::optional<component::Key> ret = std::nullopt;

    uint8_t* out = util::malloc(op.keySize);

    CF_CHECK_GT(op.iterations, 0);

    switch ( op.digestType.Get() ) {
        case CF_DIGEST("SHA1"):
            {
                /* noret */ pbkdf2_hmac_sha1(op.password.GetSize(), op.password.GetPtr(), op.iterations, op.salt.GetSize(), op.salt.GetPtr(), op.keySize, out);
                ret = component::Key(out, op.keySize);
            }
            break;
        case CF_DIGEST("SHA256"):
            {
                /* noret */ pbkdf2_hmac_sha256(op.password.GetSize(), op.password.GetPtr(), op.iterations, op.salt.GetSize(), op.salt.GetPtr(), op.keySize, out);
                ret = component::Key(out, op.keySize);
            }
            break;
    }

end:
    util::free(out);

    return ret;
}

#if defined(HAVE_LIBHOGWEED)
namespace Nettle_detail {
    const struct ecc_curve* to_ecc_curve(const uint64_t curveID) {
        switch ( curveID ) {
            case    CF_ECC_CURVE("secp192r1"):
                return nettle_get_secp_192r1();
            case    CF_ECC_CURVE("secp224r1"):
                return nettle_get_secp_224r1();
            case    CF_ECC_CURVE("secp256r1"):
                return nettle_get_secp_256r1();
            case    CF_ECC_CURVE("secp384r1"):
                return nettle_get_secp_384r1();
            case    CF_ECC_CURVE("secp521r1"):
                return nettle_get_secp_521r1();
            default:
                return nullptr;
        }
    }

    fuzzing::datasource::Datasource* ds = nullptr;

    static void nettle_fuzzer_random_func(void *ctx, size_t size, uint8_t *out) {
        (void)ctx;

        CF_ASSERT(ds != nullptr, "ds is nullptr in PRNG");

        if ( size == 0 ) {
            return;
        }

        try {
            const auto data = ds->GetData(0, size, size);
            CF_ASSERT(data.size() == size, "Unexpected data size");
            memcpy(out, data.data(), size);
            return;
        } catch ( ... ) { }

        memset(out, 1, size);
    }
}
#endif

std::optional<component::ECC_KeyPair> Nettle::OpECC_GenerateKeyPair(operation::ECC_GenerateKeyPair& op) {
    std::optional<component::ECC_KeyPair> ret = std::nullopt;
#if !defined(HAVE_LIBHOGWEED)
    (void)op;
#else
    Datasource ds(op.modifier.GetPtr(), op.modifier.GetSize());

    const struct ecc_curve* curve = nullptr;
    mpz_t priv_mpz, pub_x, pub_y;
    struct ecc_scalar priv_scalar;
    struct ecc_point pub;
    char* priv_str = nullptr, *pub_x_str = nullptr, *pub_y_str = nullptr;
    bool initialized = false;

    CF_CHECK_NE(curve = Nettle_detail::to_ecc_curve(op.curveType.Get()), nullptr);

    /* noret */ ecc_point_init(&pub, curve);
    /* noret */ ecc_scalar_init(&priv_scalar, curve);
    /* noret */ mpz_init(priv_mpz);
    /* noret */ mpz_init(pub_x);
    /* noret */ mpz_init(pub_y);

    initialized = true;

    Nettle_detail::ds = &ds;
    /* noret */ ecdsa_generate_keypair(&pub, &priv_scalar, nullptr, Nettle_detail::nettle_fuzzer_random_func);
    Nettle_detail::ds = nullptr;

    /* noret */ ecc_scalar_get(&priv_scalar, priv_mpz);
    priv_str = mpz_get_str(nullptr, 10, priv_mpz);

    /* noret */ ecc_point_get(&pub, pub_x, pub_y);
    pub_x_str = mpz_get_str(nullptr, 10, pub_x);
    pub_y_str = mpz_get_str(nullptr, 10, pub_y);

    ret = {
        std::string(priv_str),
        { std::string(pub_x_str), std::string(pub_y_str) }
    };

end:
    if ( initialized == true ) {
        /* noret */ ecc_point_clear(&pub);
        /* noret */ ecc_scalar_clear(&priv_scalar);
        /* noret */ mpz_clear(priv_mpz);
        /* noret */ mpz_clear(pub_x);
        /* noret */ mpz_clear(pub_y);
        free(priv_str);
        free(pub_x_str);
        free(pub_y_str);
    }
#endif
    return ret;
}

std::optional<component::ECC_PublicKey> Nettle::OpECC_PrivateToPublic(operation::ECC_PrivateToPublic& op) {
    std::optional<component::ECC_PublicKey> ret = std::nullopt;

#if !defined(HAVE_LIBHOGWEED)
    (void)op;
#else
    mpz_t priv_mpz, pub_x, pub_y;
    struct ecc_scalar priv_scalar;
    struct ecc_point pub;
    char* pub_x_str = nullptr, *pub_y_str = nullptr;
    const struct ecc_curve* curve = nullptr;
    bool initialized = false;

    CF_CHECK_NE(curve = Nettle_detail::to_ecc_curve(op.curveType.Get()), nullptr);

    /* noret */ ecc_point_init(&pub, curve);
    /* noret */ ecc_scalar_init(&priv_scalar, curve);
    /* noret */ mpz_init(priv_mpz);
    /* noret */ mpz_init(pub_x);
    /* noret */ mpz_init(pub_y);

    initialized = true;

    /* XXX wrong result is ToString instead of ToTrimmedString is used */
    CF_CHECK_EQ(mpz_init_set_str(priv_mpz, op.priv.ToTrimmedString().c_str(), 0), 0);
    CF_CHECK_EQ(ecc_scalar_set(&priv_scalar, priv_mpz), 1);

    /* noret */ ecc_point_mul_g(&pub, &priv_scalar);
    /* noret */ ecc_point_get(&pub, pub_x, pub_y);

    pub_x_str = mpz_get_str(nullptr, 10, pub_x);
    pub_y_str = mpz_get_str(nullptr, 10, pub_y);

    ret = { {pub_x_str, pub_y_str} };

end:
    if ( initialized == true ) {
        /* noret */ ecc_point_clear(&pub);
        /* noret */ ecc_scalar_clear(&priv_scalar);
        /* noret */ mpz_clear(priv_mpz);
        /* noret */ mpz_clear(pub_x);
        /* noret */ mpz_clear(pub_y);
        free(pub_x_str);
        free(pub_y_str);
    }
#endif

    return ret;
}

std::optional<bool> Nettle::OpECDSA_Verify(operation::ECDSA_Verify& op) {
    std::optional<bool> ret = std::nullopt;

#if !defined(HAVE_LIBHOGWEED)
    (void)op;
#else
    if ( !op.digestType.Is(CF_DIGEST("NULL")) ) {
        return ret;
    }

    mpz_t pub_x, pub_y;
    struct ecc_point pub;
    struct dsa_signature signature;
    bool initialized = false;

    const struct ecc_curve* curve = nullptr;

    CF_CHECK_NE(curve = Nettle_detail::to_ecc_curve(op.curveType.Get()), nullptr);

    /* noret */ ecc_point_init(&pub, curve);
    /* noret */ mpz_init(pub_x);
    /* noret */ mpz_init(pub_y);
    /* noret */ dsa_signature_init(&signature);
    initialized = true;

    CF_CHECK_EQ(mpz_init_set_str(pub_x, op.signature.pub.first.ToTrimmedString().c_str(), 0), 0);
    CF_CHECK_EQ(mpz_init_set_str(pub_y, op.signature.pub.second.ToTrimmedString().c_str(), 0), 0);
    CF_CHECK_EQ(ecc_point_set(&pub, pub_x, pub_y), 1);
    CF_CHECK_EQ(mpz_set_str(signature.r, op.signature.signature.first.ToTrimmedString().c_str(), 0), 0);
    CF_CHECK_EQ(mpz_set_str(signature.s, op.signature.signature.second.ToTrimmedString().c_str(), 0), 0);

    ret = ecdsa_verify(&pub, op.cleartext.GetSize(), op.cleartext.GetPtr(), &signature);
end:
    if ( initialized == true ) {
        /* noret */ mpz_clear(pub_x);
        /* noret */ mpz_clear(pub_y);
        /* noret */ ecc_point_clear(&pub);
        /* noret */ dsa_signature_clear(&signature);
    }
#endif

    return ret;
}

std::optional<component::ECDSA_Signature> Nettle::OpECDSA_Sign(operation::ECDSA_Sign& op) {
    std::optional<component::ECDSA_Signature> ret = std::nullopt;

#if !defined(HAVE_LIBHOGWEED)
    (void)op;
#else
    if ( op.UseRandomNonce() == false ) {
        return ret;
    }
    if ( !op.digestType.Is(CF_DIGEST("NULL")) ) {
        return ret;
    }

    Datasource ds(op.modifier.GetPtr(), op.modifier.GetSize());

    mpz_t priv_mpz, pub_x, pub_y;
    struct ecc_scalar priv_scalar;
    struct ecc_point pub;
    struct dsa_signature signature;
    char *pub_x_str = nullptr, *pub_y_str = nullptr, *sig_r_str = nullptr, *sig_s_str = nullptr;
    bool initialized = false;

    const struct ecc_curve* curve = nullptr;

    CF_CHECK_NE(curve = Nettle_detail::to_ecc_curve(op.curveType.Get()), nullptr);

    /* noret */ ecc_point_init(&pub, curve);
    /* noret */ mpz_init(pub_x);
    /* noret */ mpz_init(pub_y);
    /* noret */ dsa_signature_init(&signature);
    /* noret */ ecc_scalar_init(&priv_scalar, curve);
    /* noret */ mpz_init(priv_mpz);
    initialized = true;

    CF_CHECK_EQ(mpz_init_set_str(priv_mpz, op.priv.ToTrimmedString().c_str(), 0), 0);
    CF_CHECK_EQ(ecc_scalar_set(&priv_scalar, priv_mpz), 1);

    Nettle_detail::ds = &ds;
    /* noret */ ecdsa_sign(
            &priv_scalar,
            nullptr, Nettle_detail::nettle_fuzzer_random_func,
            op.cleartext.GetSize(), op.cleartext.GetPtr(), &signature);
    Nettle_detail::ds = nullptr;

    sig_r_str = mpz_get_str(nullptr, 10, signature.r);
    sig_s_str = mpz_get_str(nullptr, 10, signature.s);

    /* noret */ ecc_point_mul_g(&pub, &priv_scalar);
    /* noret */ ecc_point_get(&pub, pub_x, pub_y);

    pub_x_str = mpz_get_str(nullptr, 10, pub_x);
    pub_y_str = mpz_get_str(nullptr, 10, pub_y);

    ret = { {sig_r_str, sig_s_str}, {pub_x_str, pub_y_str} };

end:
    if ( initialized == true ) {
        /* noret */ mpz_clear(pub_x);
        /* noret */ mpz_clear(pub_y);
        /* noret */ ecc_scalar_clear(&priv_scalar);
        /* noret */ dsa_signature_clear(&signature);
        /* noret */ mpz_clear(priv_mpz);
        /* noret */ ecc_point_clear(&pub);
        free(pub_x_str);
        free(pub_y_str);
        free(sig_r_str);
        free(sig_s_str);
    }
#endif

    return ret;
}

} /* namespace module */
} /* namespace cryptofuzz */
