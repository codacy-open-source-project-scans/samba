/*
 * Copyright (c) 2016 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>
#include <roken.h>

#ifdef PKINIT

/*
 * As with the other *-ec.c files in Heimdal, this is a bit of a hack.
 *
 * The idea is to use OpenSSL for EC because hcrypto doesn't have the
 * required functionality at this time.  To do this we segregate
 * EC-using code into separate source files and then we arrange for them
 * to get the OpenSSL headers and not the conflicting hcrypto ones.
 *
 * Because of auto-generated *-private.h headers, we end up needing to
 * make sure various types are defined before we include them, thus the
 * strange header include order here.
 */

#ifdef HAVE_HCRYPTO_W_OPENSSL
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/dh.h>
#define HEIM_NO_CRYPTO_HDRS
#endif

/*
 * NO_HCRYPTO_POLLUTION -> don't refer to hcrypto type/function names
 * that we don't need in this file and which would clash with OpenSSL's
 * in ways that are difficult to address in cleaner ways.
 *
 * In the medium- to long-term what we should do is move all PK in
 * Heimdal to the newer EVP interfaces for PK and then nothing outside
 * lib/hcrypto should ever have to include OpenSSL headers, and -more
 * specifically- the only thing that should ever have to include OpenSSL
 * headers is the OpenSSL backend to hcrypto.
 */
#define NO_HCRYPTO_POLLUTION

#include "krb5_locl.h"
#include <hcrypto/des.h>
#include <cms_asn1.h>
#include <pkcs8_asn1.h>
#include <pkcs9_asn1.h>
#include <pkcs12_asn1.h>
#include <pkinit_asn1.h>
#include <asn1_err.h>

#include <der.h>

krb5_error_code
_krb5_build_authpack_subjectPK_EC(krb5_context context,
                                  krb5_pk_init_ctx ctx,
                                  AuthPack *a)
{
#ifdef HAVE_HCRYPTO_W_OPENSSL
    krb5_error_code ret;
    ECParameters ecp;
    unsigned char *p;
    size_t size;
    int xlen;

    /* copy in public key, XXX find the best curve that the server support or use the clients curve if possible */

    ecp.element = choice_ECParameters_namedCurve;
    ret = der_copy_oid(&asn1_oid_id_ec_group_secp256r1,
                       &ecp.u.namedCurve);
    if (ret)
        return ret;

    ALLOC(a->clientPublicValue->algorithm.parameters, 1);
    if (a->clientPublicValue->algorithm.parameters == NULL) {
        free_ECParameters(&ecp);
        return krb5_enomem(context);
    }
    ASN1_MALLOC_ENCODE(ECParameters, p, xlen, &ecp, &size, ret);
    free_ECParameters(&ecp);
    if (ret)
        return ret;
    if ((int)size != xlen)
        krb5_abortx(context, "asn1 internal error");

    a->clientPublicValue->algorithm.parameters->data = p;
    a->clientPublicValue->algorithm.parameters->length = size;

    /* copy in public key */

    ret = der_copy_oid(&asn1_oid_id_ecPublicKey,
                       &a->clientPublicValue->algorithm.algorithm);
    if (ret)
        return ret;

#ifdef HAVE_OPENSSL_30
    ctx->u.eckey = EVP_EC_gen(OSSL_EC_curve_nid2name(NID_X9_62_prime256v1));
#else
    ctx->u.eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (ctx->u.eckey == NULL)
        return krb5_enomem(context);

    ret = EC_KEY_generate_key(ctx->u.eckey);
    if (ret != 1)
        return EINVAL;
#endif

#ifdef HAVE_OPENSSL_30
    xlen = i2d_PublicKey(ctx->u.eckey, NULL);
#else
    xlen = i2o_ECPublicKey(ctx->u.eckey, NULL);
#endif
    if (xlen <= 0)
        return EINVAL;

    p = malloc(xlen);
    if (p == NULL)
        return krb5_enomem(context);

    a->clientPublicValue->subjectPublicKey.data = p;

#ifdef HAVE_OPENSSL_30
    xlen = i2d_PublicKey(ctx->u.eckey, &p);
#else
    xlen = i2o_ECPublicKey(ctx->u.eckey, &p);
#endif
    if (xlen <= 0) {
        a->clientPublicValue->subjectPublicKey.data = NULL;
        free(p);
        return EINVAL;
    }

    a->clientPublicValue->subjectPublicKey.length = xlen * 8;

    return 0;

    /* XXX verify that this is right with RFC3279 */
#else
    krb5_set_error_message(context, ENOTSUP,
                           N_("PKINIT: ECDH not supported", ""));
    return ENOTSUP;
#endif
}

krb5_error_code
_krb5_pk_rd_pa_reply_ecdh_compute_key(krb5_context context,
                                      krb5_pk_init_ctx ctx,
                                      const unsigned char *in,
                                      size_t in_sz,
                                      unsigned char **out,
                                      int *out_sz)
{
#ifdef HAVE_HCRYPTO_W_OPENSSL
#ifdef HAVE_OPENSSL_30
    krb5_error_code ret = 0;
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *template = NULL;
    EVP_PKEY *public = NULL;
    size_t shared_len = 0;

    if ((template = EVP_PKEY_new()) == NULL)
        ret = krb5_enomem(context);
    if (ret == 0 &&
        EVP_PKEY_copy_parameters(template, ctx->u.eckey) != 1)
        ret = krb5_enomem(context);
    if (ret == 0 && (pctx = EVP_PKEY_CTX_new(ctx->u.eckey, NULL)) == NULL)
        ret = krb5_enomem(context);
    if (ret == 0 && EVP_PKEY_derive_init(pctx) != 1)
        ret = krb5_enomem(context);
    if (ret == 0 &&
        EVP_PKEY_CTX_set_ecdh_kdf_type(pctx, EVP_PKEY_ECDH_KDF_NONE) != 1)
        ret = krb5_enomem(context);
    if (ret == 0 &&
        (public = d2i_PublicKey(EVP_PKEY_EC, &template, &in, in_sz)) == NULL)
        krb5_set_error_message(context,
                               ret = HX509_PARSING_KEY_FAILED,
                               "PKINIT: Can't parse the KDC's ECDH public key");
    if (ret == 0 &&
        EVP_PKEY_derive_set_peer_ex(pctx, public, 1) != 1)
        krb5_set_error_message(context,
                               ret = KRB5KRB_ERR_GENERIC,
                               "Could not derive ECDH shared secret for PKINIT key exchange "
                               "(EVP_PKEY_derive_set_peer_ex)");
    if (ret == 0 &&
        (EVP_PKEY_derive(pctx, NULL, &shared_len) != 1 || shared_len == 0))
        krb5_set_error_message(context,
                               ret = KRB5KRB_ERR_GENERIC,
                               "Could not derive ECDH shared secret for PKINIT key exchange "
                               "(EVP_PKEY_derive to get length)");
    if (ret == 0 && shared_len > INT_MAX)
        krb5_set_error_message(context,
                               ret = KRB5KRB_ERR_GENERIC,
                               "Could not derive ECDH shared secret for PKINIT key exchange "
                               "(shared key too large)");
    if (ret == 0 && (*out = malloc(shared_len)) == NULL)
        ret = krb5_enomem(context);
    if (ret == 0 && EVP_PKEY_derive(pctx, *out, &shared_len) != 1)
        krb5_set_error_message(context,
                               ret = KRB5KRB_ERR_GENERIC,
                               "Could not derive ECDH shared secret for PKINIT key exchange "
                               "(EVP_PKEY_derive)");
    if (ret == 0)
        *out_sz = shared_len;
    EVP_PKEY_CTX_free(pctx); // move
    EVP_PKEY_free(template);

    return ret;
#else
    krb5_error_code ret = 0;
    int dh_gen_keylen;

    const EC_GROUP *group;
    EC_KEY *public = NULL;

    group = EC_KEY_get0_group(ctx->u.eckey);

    public = EC_KEY_new();
    if (public == NULL)
        return krb5_enomem(context);
    if (EC_KEY_set_group(public, group) != 1) {
        EC_KEY_free(public);
        return krb5_enomem(context);
    }

    if (o2i_ECPublicKey(&public, &in, in_sz) == NULL) {
        EC_KEY_free(public);
        ret = KRB5KRB_ERR_GENERIC;
        krb5_set_error_message(context, ret,
                               N_("PKINIT: Can't parse ECDH public key", ""));
        return ret;
    }

    *out_sz = (EC_GROUP_get_degree(group) + 7) / 8;
    if (*out_sz < 0)
        return EOVERFLOW;
    *out = malloc(*out_sz);
    if (*out == NULL) {
        EC_KEY_free(public);
        return krb5_enomem(context);
    }
    dh_gen_keylen = ECDH_compute_key(*out, *out_sz,
                                     EC_KEY_get0_public_key(public),
                                     ctx->u.eckey, NULL);
    EC_KEY_free(public);
    if (dh_gen_keylen <= 0) {
        ret = KRB5KRB_ERR_GENERIC;
        dh_gen_keylen = 0;
        krb5_set_error_message(context, ret,
                               N_("PKINIT: Can't compute ECDH public key", ""));
        free(*out);
        *out = NULL;
        *out_sz = 0;
    }
    *out_sz = dh_gen_keylen;

    return ret;
#endif
#else
    krb5_set_error_message(context, ENOTSUP,
                           N_("PKINIT: ECDH not supported", ""));
    return ENOTSUP;
#endif
}

void
_krb5_pk_eckey_free(void *eckey)
{
#ifdef HAVE_HCRYPTO_W_OPENSSL
#ifdef HAVE_OPENSSL_30
    EVP_PKEY_free(eckey);
#else
    EC_KEY_free(eckey);
#endif
#endif
}

#else

static char lib_krb5_pkinit_ec_c = '\0';

#endif
