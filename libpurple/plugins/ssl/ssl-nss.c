/**
 * @file ssl-nss.c Mozilla NSS SSL plugin.
 *
 * purple
 *
 * Copyright (C) 2003 Christian Hammond <chipx86@gnupdate.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include "internal.h"
#include "debug.h"
#include "certificate.h"
#include "plugin.h"
#include "sslconn.h"
#include "util.h"
#include "version.h"

#define SSL_NSS_PLUGIN_ID "ssl-nss"

#ifdef _WIN32
# ifndef HAVE_LONG_LONG
#define HAVE_LONG_LONG
/* WINDDK_BUILD is defined because the checks around usage of
 * intrisic functions are wrong in nspr */
#define WINDDK_BUILD
# endif
#else
/* TODO: Why is this done?
 * This is probably being overridden by <nspr.h> (prcpucfg.h) on *nix OSes */
#undef HAVE_LONG_LONG /* Make Mozilla less angry. If angry, Mozilla SMASH! */
#endif

#include <nspr.h>
#include <nss.h>
#include <nssb64.h>
#include <ocsp.h>
#include <pk11func.h>
#include <prio.h>
#include <secerr.h>
#include <secmod.h>
#include <ssl.h>
#include <sslerr.h>
#include <sslproto.h>

/* There's a bug in some versions of this header that requires that some of
   the headers above be included first. This is true for at least libnss
   3.15.4. */
#include <certdb.h>

/* This is defined in NSPR's <private/pprio.h>, but to avoid including a
 * private header we duplicate the prototype here */
NSPR_API(PRFileDesc*)  PR_ImportTCPSocket(PRInt32 osfd);

typedef struct
{
	PRFileDesc *fd;
	PRFileDesc *in;
	guint handshake_handler;
	guint handshake_timer;
} PurpleSslNssData;

#define PURPLE_SSL_NSS_DATA(gsc) ((PurpleSslNssData *)gsc->private_data)

static const PRIOMethods *_nss_methods = NULL;
static PRDescIdentity _identity;
static PurpleCertificateScheme x509_nss;

/* Thank you, Evolution */
static void
set_errno(int code)
{
	/* FIXME: this should handle more. */
	switch (code) {
	case PR_INVALID_ARGUMENT_ERROR:
		errno = EINVAL;
		break;
	case PR_PENDING_INTERRUPT_ERROR:
		errno = EINTR;
		break;
	case PR_IO_PENDING_ERROR:
		errno = EAGAIN;
		break;
	case PR_WOULD_BLOCK_ERROR:
		errno = EAGAIN;
		/*errno = EWOULDBLOCK; */
		break;
	case PR_IN_PROGRESS_ERROR:
		errno = EINPROGRESS;
		break;
	case PR_ALREADY_INITIATED_ERROR:
		errno = EALREADY;
		break;
	case PR_NETWORK_UNREACHABLE_ERROR:
		errno = EHOSTUNREACH;
		break;
	case PR_CONNECT_REFUSED_ERROR:
		errno = ECONNREFUSED;
		break;
	case PR_CONNECT_TIMEOUT_ERROR:
	case PR_IO_TIMEOUT_ERROR:
		errno = ETIMEDOUT;
		break;
	case PR_NOT_CONNECTED_ERROR:
		errno = ENOTCONN;
		break;
	case PR_CONNECT_RESET_ERROR:
		errno = ECONNRESET;
		break;
	case PR_IO_ERROR:
	default:
		errno = EIO;
		break;
	}
}

static gchar *get_error_text(void)
{
	PRInt32 len = PR_GetErrorTextLength();
	gchar *ret = NULL;

	if (len > 0) {
		ret = g_malloc(len + 1);
		len = PR_GetErrorText(ret);
		ret[len] = '\0';
	}

	return ret;
}

static void ssl_nss_init_ciphers(void) {
	const PRUint16 *cipher;

	/* Log the available and enabled Ciphers */
	for (cipher = SSL_GetImplementedCiphers(); *cipher != 0; ++cipher) {
		const PRUint16 suite = *cipher;
		SECStatus rv;
		PRBool enabled;
		SSLCipherSuiteInfo info;

		rv = SSL_CipherPrefGetDefault(suite, &enabled);
		if (rv != SECSuccess) {
			gchar *error_txt = get_error_text();
			purple_debug_warning("nss",
					"SSL_CipherPrefGetDefault didn't like value 0x%04x: %s\n",
					suite, error_txt);
			g_free(error_txt);
			continue;
		}
		rv = SSL_GetCipherSuiteInfo(suite, &info, (int)(sizeof info));
		if (rv != SECSuccess) {
			gchar *error_txt = get_error_text();
			purple_debug_warning("nss",
					"SSL_GetCipherSuiteInfo didn't like value 0x%04x: %s\n",
					suite, error_txt);
			g_free(error_txt);
			continue;
		}
		purple_debug_info("nss", "Cipher - %s: %s\n",
				info.cipherSuiteName,
				enabled ? "Enabled" : "Disabled");
	}
}

static void
ssl_nss_init_nss(void)
{
#if NSS_VMAJOR > 3 || ( NSS_VMAJOR == 3 && NSS_VMINOR >= 14 )
	SSLVersionRange supported, enabled;
#endif /* NSS >= 3.14 */

	PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
	NSS_NoDB_Init(".");
#if (NSS_VMAJOR == 3 && (NSS_VMINOR < 15 || (NSS_VMINOR == 15 && NSS_VPATCH < 2)))
	NSS_SetDomesticPolicy();
#endif /* NSS < 3.15.2 */

	ssl_nss_init_ciphers();

#if NSS_VMAJOR > 3 || ( NSS_VMAJOR == 3 && NSS_VMINOR >= 14 )
	/* Get the ranges of supported and enabled SSL versions */
	if ((SSL_VersionRangeGetSupported(ssl_variant_stream, &supported) == SECSuccess) &&
			(SSL_VersionRangeGetDefault(ssl_variant_stream, &enabled) == SECSuccess)) {
		purple_debug_info("nss", "TLS supported versions: "
				"0x%04hx through 0x%04hx\n", supported.min, supported.max);
		purple_debug_info("nss", "TLS versions allowed by default: "
				"0x%04hx through 0x%04hx\n", enabled.min, enabled.max);
	}
#endif /* NSS >= 3.14 */

	/** Disable OCSP Checking until we can make that use our HTTP & Proxy stuff */
	CERT_EnableOCSPChecking(PR_FALSE);

	_identity = PR_GetUniqueIdentity("Purple");
	_nss_methods = PR_GetDefaultIOMethods();

}

static SECStatus
ssl_auth_cert(void *arg, PRFileDesc *socket, PRBool checksig, PRBool is_server)
{
	/* We just skip cert verification here, and will verify the whole chain
	 * in ssl_nss_handshake_cb, after the handshake is complete.
	 *
	 * The problem is, purple_certificate_verify is asynchronous and
	 * ssl_auth_cert should return the result synchronously (it may ask the
	 * user, if an unknown certificate should be trusted or not).
	 *
	 * Ideally, SSL_AuthCertificateHook/ssl_auth_cert should decide
	 * immediately, if the certificate chain is already trusted and possibly
	 * SSL_BadCertHook to deal with unknown certificates.
	 *
	 * Current implementation may not be ideal, but is no less secure in
	 * terms of MITM attack.
	 */
	return SECSuccess;
}

static gboolean
ssl_nss_init(void)
{
   return TRUE;
}

static void
ssl_nss_uninit(void)
{
	NSS_Shutdown();
	PR_Cleanup();

	_nss_methods = NULL;
}

static void
ssl_nss_verified_cb(PurpleCertificateVerificationStatus st,
		       gpointer userdata)
{
	PurpleSslConnection *gsc = (PurpleSslConnection *) userdata;

	if (st == PURPLE_CERTIFICATE_VALID) {
		/* Certificate valid? Good! Do the connection! */
		gsc->connect_cb(gsc->connect_cb_data, gsc, PURPLE_INPUT_READ);
	} else {
		/* Otherwise, signal an error */
		if(gsc->error_cb != NULL)
			gsc->error_cb(gsc, PURPLE_SSL_CERTIFICATE_INVALID,
				      gsc->connect_cb_data);
		purple_ssl_close(gsc);
	}
}

/** Transforms an NSS containing an X.509 certificate into a Certificate instance
 *
 * @param cert   Certificate to transform
 * @return A newly allocated Certificate
 */
static PurpleCertificate *
x509_import_from_nss(CERTCertificate* cert)
{
	/* New certificate to return */
	PurpleCertificate * crt;

	/* Allocate the certificate and load it with data */
	crt = g_new0(PurpleCertificate, 1);
	crt->scheme = &x509_nss;
	crt->data = CERT_DupCertificate(cert);

	return crt;
}

static GList *
ssl_nss_get_peer_certificates(PRFileDesc *socket, PurpleSslConnection * gsc)
{
	CERTCertList *peerChain;
	CERTCertListNode *cursor;
	CERTCertificate *curcert;
	PurpleCertificate * newcrt;

	/* List of Certificate instances to return */
	GList * peer_certs = NULL;

	peerChain = SSL_PeerCertificateChain(socket);
	if (peerChain == NULL) {
		purple_debug_error("nss", "no peer certificates\n");
		return NULL;
	}

	for (cursor = CERT_LIST_HEAD(peerChain); !CERT_LIST_END(cursor, peerChain); cursor = CERT_LIST_NEXT(cursor)) {
		curcert = cursor->cert;
		if (!curcert) {
			purple_debug_error("nss", "cursor->cert == NULL\n");
			break;
		}
		purple_debug_info("nss", "subject=%s issuer=%s\n", curcert->subjectName,
						  curcert->issuerName  ? curcert->issuerName : "(null)");
		newcrt = x509_import_from_nss(curcert);
		peer_certs = g_list_append(peer_certs, newcrt);
	}
	CERT_DestroyCertList(peerChain);

	return peer_certs;
}

/*
 * Ideally this information would be exposed to the UI somehow, but for now we
 * just print it to the debug log
 */
static void
print_security_info(PRFileDesc *fd)
{
	SECStatus result;
	SSLChannelInfo channel;
	SSLCipherSuiteInfo suite;

	result = SSL_GetChannelInfo(fd, &channel, sizeof channel);
	if (result == SECSuccess && channel.length == sizeof channel
			&& channel.cipherSuite) {
		result = SSL_GetCipherSuiteInfo(channel.cipherSuite,
				&suite, sizeof suite);

		if (result == SECSuccess) {
			purple_debug_info("nss", "SSL version %d.%d using "
					"%d-bit %s with %d-bit %s MAC\n"
					"Server Auth: %d-bit %s, "
					"Key Exchange: %d-bit %s, "
					"Compression: %s\n"
					"Cipher Suite Name: %s\n",
					channel.protocolVersion >> 8,
					channel.protocolVersion & 0xff,
					suite.effectiveKeyBits,
					suite.symCipherName,
					suite.macBits,
					suite.macAlgorithmName,
					channel.authKeyBits,
					suite.authAlgorithmName,
					channel.keaKeyBits, suite.keaTypeName,
					channel.compressionMethodName,
					suite.cipherSuiteName);
		}
	}
}


static void
ssl_nss_handshake_cb(gpointer data, int fd, PurpleInputCondition cond)
{
	PurpleSslConnection *gsc = (PurpleSslConnection *)data;
	PurpleSslNssData *nss_data = gsc->private_data;

	/* I don't think this the best way to do this...
	 * It seems to work because it'll eventually use the cached value
	 */
	if(SSL_ForceHandshake(nss_data->in) != SECSuccess) {
		gchar *error_txt;
		set_errno(PR_GetError());
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;

		error_txt = get_error_text();
		purple_debug_error("nss", "Handshake failed %s (%d)\n", error_txt ? error_txt : "", PR_GetError());
		g_free(error_txt);

		if (gsc->error_cb != NULL)
			gsc->error_cb(gsc, PURPLE_SSL_HANDSHAKE_FAILED, gsc->connect_cb_data);

		purple_ssl_close(gsc);

		return;
	}

	print_security_info(nss_data->in);

	purple_input_remove(nss_data->handshake_handler);
	nss_data->handshake_handler = 0;

	/* If a Verifier was given, hand control over to it */
	if (gsc->verifier) {
		GList *peers;
		/* First, get the peer cert chain */
		peers = ssl_nss_get_peer_certificates(nss_data->in, gsc);

		/* Now kick off the verification process */
		purple_certificate_verify(gsc->verifier,
				gsc->host,
				peers,
				ssl_nss_verified_cb,
				gsc);

		purple_certificate_destroy_list(peers);
	} else {
		/* Otherwise, just call the "connection complete"
		 * callback. The verification was already done with
		 * SSL_AuthCertificate, the default verifier
		 * (SSL_AuthCertificateHook was not called in ssl_nss_connect).
		 */
		gsc->connect_cb(gsc->connect_cb_data, gsc, cond);
	}
}

static gboolean
start_handshake_cb(gpointer data)
{
	PurpleSslConnection *gsc = data;
	PurpleSslNssData *nss_data = PURPLE_SSL_NSS_DATA(gsc);

	nss_data->handshake_timer = 0;

	ssl_nss_handshake_cb(gsc, gsc->fd, PURPLE_INPUT_READ);
	return FALSE;
}

static void
ssl_nss_connect(PurpleSslConnection *gsc)
{
	PurpleSslNssData *nss_data = g_new0(PurpleSslNssData, 1);
	PRSocketOptionData socket_opt;

	gsc->private_data = nss_data;

	nss_data->fd = PR_ImportTCPSocket(gsc->fd);

	if (nss_data->fd == NULL)
	{
		purple_debug_error("nss", "nss_data->fd == NULL!\n");

		if (gsc->error_cb != NULL)
			gsc->error_cb(gsc, PURPLE_SSL_CONNECT_FAILED, gsc->connect_cb_data);

		purple_ssl_close((PurpleSslConnection *)gsc);

		return;
	}

	socket_opt.option = PR_SockOpt_Nonblocking;
	socket_opt.value.non_blocking = PR_TRUE;

	if (PR_SetSocketOption(nss_data->fd, &socket_opt) != PR_SUCCESS) {
		gchar *error_txt = get_error_text();
		purple_debug_warning("nss", "unable to set socket into non-blocking mode: %s (%d)\n", error_txt ? error_txt : "", PR_GetError());
		g_free(error_txt);
	}

	nss_data->in = SSL_ImportFD(NULL, nss_data->fd);

	if (nss_data->in == NULL)
	{
		purple_debug_error("nss", "nss_data->in == NUL!\n");

		if (gsc->error_cb != NULL)
			gsc->error_cb(gsc, PURPLE_SSL_CONNECT_FAILED, gsc->connect_cb_data);

		purple_ssl_close((PurpleSslConnection *)gsc);

		return;
	}

	SSL_OptionSet(nss_data->in, SSL_SECURITY,            PR_TRUE);
	SSL_OptionSet(nss_data->in, SSL_HANDSHAKE_AS_CLIENT, PR_TRUE);

	/* If we have our internal verifier set up, use it. Otherwise,
	 * use default. */
	if (gsc->verifier != NULL)
		SSL_AuthCertificateHook(nss_data->in, ssl_auth_cert, NULL);

	if(gsc->host)
		SSL_SetURL(nss_data->in, gsc->host);

#if 0
	/* This seems like it'd the be the correct way to implement the
	nonblocking stuff, but it doesn't seem to work */
	SSL_HandshakeCallback(nss_data->in,
		(SSLHandshakeCallback) ssl_nss_handshake_cb, gsc);
#endif
	SSL_ResetHandshake(nss_data->in, PR_FALSE);

	nss_data->handshake_handler = purple_input_add(gsc->fd,
		PURPLE_INPUT_READ, ssl_nss_handshake_cb, gsc);

	nss_data->handshake_timer = purple_timeout_add(0, start_handshake_cb, gsc);
}

static void
ssl_nss_close(PurpleSslConnection *gsc)
{
	PurpleSslNssData *nss_data = PURPLE_SSL_NSS_DATA(gsc);

	if(!nss_data)
		return;

	if (nss_data->in) {
		PR_Close(nss_data->in);
		gsc->fd = -1;
	} else if (nss_data->fd) {
		PR_Close(nss_data->fd);
		gsc->fd = -1;
	}

	if (nss_data->handshake_handler)
		purple_input_remove(nss_data->handshake_handler);

	if (nss_data->handshake_timer)
		purple_timeout_remove(nss_data->handshake_timer);

	g_free(nss_data);
	gsc->private_data = NULL;
}

static size_t
ssl_nss_read(PurpleSslConnection *gsc, void *data, size_t len)
{
	PRInt32 ret;
	PurpleSslNssData *nss_data = PURPLE_SSL_NSS_DATA(gsc);

	if (!nss_data)
		return 0;

	ret = PR_Read(nss_data->in, data, len);

	if (ret == -1)
		set_errno(PR_GetError());

	return ret;
}

static size_t
ssl_nss_write(PurpleSslConnection *gsc, const void *data, size_t len)
{
	PRInt32 ret;
	PurpleSslNssData *nss_data = PURPLE_SSL_NSS_DATA(gsc);

	if(!nss_data)
		return 0;

	ret = PR_Write(nss_data->in, data, len);

	if (ret == -1)
		set_errno(PR_GetError());

	return ret;
}

static GList *
ssl_nss_peer_certs(PurpleSslConnection *gsc)
{
#if 0
	PurpleSslNssData *nss_data = PURPLE_SSL_NSS_DATA(gsc);
	CERTCertificate *cert;
/*
	GList *chain = NULL;
	void *pinArg;
	SECStatus status;
*/

	/* TODO: this is a blind guess */
	cert = SSL_PeerCertificate(nss_data->fd);

	if (cert)
		CERT_DestroyCertificate(cert);
#endif



	return NULL;
}

/************************************************************************/
/* X.509 functionality                                                  */
/************************************************************************/
static PurpleCertificateScheme x509_nss;

/** Helpr macro to retrieve the NSS certdata from a PurpleCertificate */
#define X509_NSS_DATA(pcrt) ( (CERTCertificate * ) (pcrt->data) )

/** Imports a PEM-formatted X.509 certificate from the specified file.
 * @param filename Filename to import from. Format is PEM
 *
 * @return A newly allocated Certificate structure of the x509_nss scheme
 */
static PurpleCertificate *
x509_import_from_file(const gchar *filename)
{
	gchar *rawcert;
	gsize len = 0;
	CERTCertificate *crt_dat;
	PurpleCertificate *crt;

	g_return_val_if_fail(filename != NULL, NULL);

	purple_debug_info("nss/x509",
			  "Loading certificate from %s\n",
			  filename);

	/* Load the raw data up */
	if (!g_file_get_contents(filename,
				 &rawcert, &len,
				 NULL)) {
		purple_debug_error("nss/x509", "Unable to read certificate file.\n");
		return NULL;
	}

	if (len == 0) {
		purple_debug_error("nss/x509",
				"Certificate file has no contents!\n");
		if (rawcert)
			g_free(rawcert);
		return NULL;
	}

	/* Decode the certificate */
	crt_dat = CERT_DecodeCertFromPackage(rawcert, len);
	g_free(rawcert);

	g_return_val_if_fail(crt_dat != NULL, NULL);

	crt = g_new0(PurpleCertificate, 1);
	crt->scheme = &x509_nss;
	crt->data = crt_dat;

	return crt;
}

/** Imports a number of PEM-formatted X.509 certificates from the specified file.
 * @param filename Filename to import from. Format is PEM
 *
 * @return A GSList of newly allocated Certificate structures of the x509_nss scheme
 */
static GSList *
x509_importcerts_from_file(const gchar *filename)
{
	gchar *rawcert, *begin, *end;
	gsize len = 0;
	GSList *crts = NULL;
	CERTCertificate *crt_dat;
	PurpleCertificate *crt;

	g_return_val_if_fail(filename != NULL, NULL);

	purple_debug_info("nss/x509",
			  "Loading certificate from %s\n",
			  filename);

	/* Load the raw data up */
	if (!g_file_get_contents(filename,
				 &rawcert, &len,
				 NULL)) {
		purple_debug_error("nss/x509", "Unable to read certificate file.\n");
		return NULL;
	}

	if (len == 0) {
		purple_debug_error("nss/x509",
				"Certificate file has no contents!\n");
		if (rawcert)
			g_free(rawcert);
		return NULL;
	}

	begin = rawcert;
	while((end = strstr(begin, "-----END CERTIFICATE-----")) != NULL) {
		end += sizeof("-----END CERTIFICATE-----")-1;
		/* Decode the certificate */
		crt_dat = CERT_DecodeCertFromPackage(begin, (end-begin));

		g_return_val_if_fail(crt_dat != NULL, NULL);

		crt = g_new0(PurpleCertificate, 1);
		crt->scheme = &x509_nss;
		crt->data = crt_dat;
		crts = g_slist_prepend(crts, crt);
		begin = end;
	}
	g_free(rawcert);

	return crts;
}
/**
 * Exports a PEM-formatted X.509 certificate to the specified file.
 * @param filename Filename to export to. Format will be PEM
 * @param crt      Certificate to export
 *
 * @return TRUE if success, otherwise FALSE
 */
/* This function should not be so complicated, but NSS doesn't seem to have a
   "convert yon certificate to PEM format" function. */
static gboolean
x509_export_certificate(const gchar *filename, PurpleCertificate *crt)
{
	CERTCertificate *crt_dat;
	SECItem *dercrt;
	gchar *b64crt;
	gchar *pemcrt;
	gboolean ret = FALSE;

	g_return_val_if_fail(filename, FALSE);
	g_return_val_if_fail(crt, FALSE);
	g_return_val_if_fail(crt->scheme == &x509_nss, FALSE);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, FALSE);

	purple_debug_info("nss/x509",
			  "Exporting certificate to %s\n", filename);

	/* First, use NSS voodoo to create a DER-formatted certificate */
	dercrt = SEC_ASN1EncodeItem(NULL, NULL, crt_dat,
				    SEC_ASN1_GET(SEC_SignedCertificateTemplate));
	g_return_val_if_fail(dercrt != NULL, FALSE);

	/* Now encode it to b64 */
	b64crt = NSSBase64_EncodeItem(NULL, NULL, 0, dercrt);
	SECITEM_FreeItem(dercrt, PR_TRUE);
	g_return_val_if_fail(b64crt, FALSE);

	/* Wrap it in nice PEM header things */
	pemcrt = g_strdup_printf("-----BEGIN CERTIFICATE-----\n%s\n-----END CERTIFICATE-----\n", b64crt);
	PORT_Free(b64crt); /* Notice that b64crt was allocated by an NSS
			      function; hence, we'll let NSPR free it. */

	/* Finally, dump the silly thing to a file. */
	ret =  purple_util_write_data_to_file_absolute(filename, pemcrt, -1);

	g_free(pemcrt);

	return ret;
}

static PurpleCertificate *
x509_copy_certificate(PurpleCertificate *crt)
{
	CERTCertificate *crt_dat;
	PurpleCertificate *newcrt;

	g_return_val_if_fail(crt, NULL);
	g_return_val_if_fail(crt->scheme == &x509_nss, NULL);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, NULL);

	/* Create the certificate copy */
	newcrt = g_new0(PurpleCertificate, 1);
	newcrt->scheme = &x509_nss;
	/* NSS does refcounting automatically */
	newcrt->data = CERT_DupCertificate(crt_dat);

	return newcrt;
}

/** Frees a Certificate
 *
 *  Destroys a Certificate's internal data structures and frees the pointer
 *  given.
 *  @param crt  Certificate instance to be destroyed. It WILL NOT be destroyed
 *              if it is not of the correct CertificateScheme. Can be NULL
 *
 */
static void
x509_destroy_certificate(PurpleCertificate * crt)
{
	CERTCertificate *crt_dat;

	g_return_if_fail(crt);
	g_return_if_fail(crt->scheme == &x509_nss);

	crt_dat = X509_NSS_DATA(crt);
	g_return_if_fail(crt_dat);

	/* Finally we have the certificate. So let's kill it */
	/* NSS does refcounting automatically */
	CERT_DestroyCertificate(crt_dat);

	/* Delete the PurpleCertificate as well */
	g_free(crt);
}

/** Determines whether one certificate has been issued and signed by another
 *
 * @param crt       Certificate to check the signature of
 * @param issuer    Issuer's certificate
 *
 * @return TRUE if crt was signed and issued by issuer, otherwise FALSE
 * @TODO  Modify this function to return a reason for invalidity?
 */
static gboolean
x509_signed_by(PurpleCertificate * crt,
	       PurpleCertificate * issuer)
{
	CERTCertificate *subjectCert;
	CERTCertificate *issuerCert;
	SECStatus st;

	issuerCert = X509_NSS_DATA(issuer);
	g_return_val_if_fail(issuerCert, FALSE);

	subjectCert = X509_NSS_DATA(crt);
	g_return_val_if_fail(subjectCert, FALSE);

	if (subjectCert->issuerName == NULL || issuerCert->subjectName == NULL
			|| PORT_Strcmp(subjectCert->issuerName, issuerCert->subjectName) != 0)
		return FALSE;
	st = CERT_VerifySignedData(&subjectCert->signatureWrap, issuerCert, PR_Now(), NULL);
	return st == SECSuccess;
}

static GByteArray *
x509_shasum(PurpleCertificate *crt, SECOidTag algo)
{
	CERTCertificate *crt_dat;
	size_t hashlen = (algo == SEC_OID_SHA1) ? 20 : 32;
	GByteArray *hash;
	SECItem *derCert; /* DER representation of the cert */
	SECStatus st;

	g_return_val_if_fail(crt, NULL);
	g_return_val_if_fail(crt->scheme == &x509_nss, NULL);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, NULL);

	/* Get the certificate DER representation */
	derCert = &(crt_dat->derCert);

	/* Make a hash! */
	hash = g_byte_array_sized_new(hashlen);
	/* glib leaves the size as 0 by default */
	hash->len = hashlen;

	st = PK11_HashBuf(algo, hash->data,
			  derCert->data, derCert->len);

	/* Check for errors */
	if (st != SECSuccess) {
		g_byte_array_free(hash, TRUE);
		purple_debug_error("nss/x509",
				   "Error: hashing failed!\n");
		return NULL;
	}

	return hash;
}

static GByteArray *
x509_sha1sum(PurpleCertificate *crt)
{
	return x509_shasum(crt, SEC_OID_SHA1);
}

static GByteArray *
x509_sha256sum(PurpleCertificate *crt)
{
	return x509_shasum(crt, SEC_OID_SHA256);
}

static gchar *
x509_dn (PurpleCertificate *crt)
{
	CERTCertificate *crt_dat;

	g_return_val_if_fail(crt, NULL);
	g_return_val_if_fail(crt->scheme == &x509_nss, NULL);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, NULL);

	return g_strdup(crt_dat->subjectName);
}

static gchar *
x509_issuer_dn (PurpleCertificate *crt)
{
	CERTCertificate *crt_dat;

	g_return_val_if_fail(crt, NULL);
	g_return_val_if_fail(crt->scheme == &x509_nss, NULL);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, NULL);

	return g_strdup(crt_dat->issuerName);
}

static gchar *
x509_common_name (PurpleCertificate *crt)
{
	CERTCertificate *crt_dat;
	char *nss_cn;
	gchar *ret_cn;

	g_return_val_if_fail(crt, NULL);
	g_return_val_if_fail(crt->scheme == &x509_nss, NULL);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, NULL);

	/* Q:
	   Why get a newly allocated string out of NSS, strdup it, and then
	   return the new copy?

	   A:
	   The NSS LXR docs state that I should use the NSPR free functions on
	   the strings that the NSS cert functions return. Since the libpurple
	   API expects a g_free()-able string, we make our own copy and return
	   that.

	   NSPR is something of a prima donna. */

	nss_cn = CERT_GetCommonName( &(crt_dat->subject) );
	ret_cn = g_strdup(nss_cn);
	PORT_Free(nss_cn);

	return ret_cn;
}

static gboolean
x509_check_name (PurpleCertificate *crt, const gchar *name)
{
	CERTCertificate *crt_dat;
	SECStatus st;

	g_return_val_if_fail(crt, FALSE);
	g_return_val_if_fail(crt->scheme == &x509_nss, FALSE);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, FALSE);

	st = CERT_VerifyCertName(crt_dat, name);

	if (st == SECSuccess) {
		return TRUE;
	}
	else if (st == SECFailure) {
		return FALSE;
	}

	/* If we get here...bad things! */
	purple_debug_error("nss/x509",
			   "x509_check_name fell through where it shouldn't "
			   "have.\n");
	return FALSE;
}

static gboolean
x509_times (PurpleCertificate *crt, time_t *activation, time_t *expiration)
{
	CERTCertificate *crt_dat;
	PRTime nss_activ, nss_expir;
	SECStatus cert_times_success;

	g_return_val_if_fail(crt, FALSE);
	g_return_val_if_fail(crt->scheme == &x509_nss, FALSE);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, FALSE);

	/* Extract the times into ugly PRTime thingies */
	/* TODO: Maybe this shouldn't throw an error? */
	cert_times_success = CERT_GetCertTimes(crt_dat,
						&nss_activ, &nss_expir);
	g_return_val_if_fail(cert_times_success == SECSuccess, FALSE);

	/* NSS's native PRTime type *almost* corresponds to time_t; however,
	   it measures *microseconds* since the epoch, not seconds. Hence
	   the funny conversion. */
	nss_activ = nss_activ / 1000000;
	nss_expir = nss_expir / 1000000;

	if (activation) {
		*activation = nss_activ;
#if SIZEOF_TIME_T == 4
		/** Hack to deal with dates past the 32-bit barrier.
		    Handling is different for signed vs unsigned 32-bit types.
		 */
		if (*activation != nss_activ) {
			if (nss_activ < 0) {
				purple_debug_warning("nss",
					"Setting Activation Date to epoch to handle pre-epoch value\n");
				*activation = 0;
			} else {
				purple_debug_error("nss",
					"Activation date past 32-bit barrier, forcing invalidity\n");
				return FALSE;
			}
		}
#endif
	}
	if (expiration) {
		*expiration = nss_expir;
#if SIZEOF_TIME_T == 4
		if (*expiration != nss_expir) {
			if (*expiration < nss_expir) {
				if (*expiration < 0) {
					purple_debug_warning("nss",
						"Setting Expiration Date to 32-bit signed max\n");
					*expiration = PR_INT32_MAX;
				} else {
					purple_debug_warning("nss",
						"Setting Expiration Date to 32-bit unsigned max\n");
					*expiration = PR_UINT32_MAX;
				}
			} else {
				purple_debug_error("nss",
					"Expiration date prior to unix epoch, forcing invalidity\n");
				return FALSE;
			}
		}
#endif
	}

	return TRUE;
}

static gboolean
x509_register_trusted_tls_cert(PurpleCertificate *crt, gboolean ca)
{
	CERTCertDBHandle *certdb = CERT_GetDefaultCertDB();
	CERTCertificate *crt_dat;
	CERTCertTrust trust;

	g_return_val_if_fail(crt, FALSE);
	g_return_val_if_fail(crt->scheme == &x509_nss, FALSE);

	crt_dat = X509_NSS_DATA(crt);
	g_return_val_if_fail(crt_dat, FALSE);

	purple_debug_info("nss", "Trusting %s\n", crt_dat->subjectName);

	if (ca && !CERT_IsCACert(crt_dat, NULL)) {
		purple_debug_error("nss",
			"Refusing to set non-CA cert as trusted CA\n");
		return FALSE;
	}

	if (crt_dat->isperm) {
		purple_debug_info("nss",
			"Skipping setting trust for cert in permanent DB\n");
		return TRUE;
	}

	if (ca) {
		trust.sslFlags = CERTDB_TRUSTED_CA | CERTDB_TRUSTED_CLIENT_CA;
	} else {
		trust.sslFlags = CERTDB_TRUSTED;
	}
	trust.emailFlags = 0;
	trust.objectSigningFlags = 0;

	CERT_ChangeCertTrust(certdb, crt_dat, &trust);

	return TRUE;
}

static void x509_verify_cert(PurpleCertificateVerificationRequest *vrq, PurpleCertificateInvalidityFlags *flags)
{
	CERTCertDBHandle *certdb = CERT_GetDefaultCertDB();
	CERTCertificate *crt_dat;
	PRTime now = PR_Now();
	SECStatus rv;
	PurpleCertificate *first_cert = vrq->cert_chain->data;
	CERTVerifyLog log;
	gboolean self_signed = FALSE;

	crt_dat = X509_NSS_DATA(first_cert);

	log.arena = PORT_NewArena(512);
	log.head = log.tail = NULL;
	log.count = 0;
	rv = CERT_VerifyCert(certdb, crt_dat, PR_TRUE, certUsageSSLServer, now, NULL, &log);

	if (rv != SECSuccess || log.count > 0) {
		CERTVerifyLogNode *node   = NULL;
		unsigned int depth = (unsigned int)-1;

		if (crt_dat->isRoot) {
			self_signed = TRUE;
			*flags |= PURPLE_CERTIFICATE_SELF_SIGNED;
		}

		/* Handling of untrusted, etc. modeled after
		 * source/security/manager/ssl/src/TransportSecurityInfo.cpp in Firefox
		 */
		for (node = log.head; node; node = node->next) {
			if (depth != node->depth) {
				depth = node->depth;
				purple_debug_error("nss", "CERT %d. %s %s:\n", depth,
					node->cert->subjectName,
					depth ? "[Certificate Authority]": "");
			}
			purple_debug_error("nss", "  ERROR %ld: %s\n", node->error,
				PR_ErrorToName(node->error));
			switch (node->error) {
				case SEC_ERROR_EXPIRED_CERTIFICATE:
					*flags |= PURPLE_CERTIFICATE_EXPIRED;
					break;
				case SEC_ERROR_REVOKED_CERTIFICATE:
					*flags |= PURPLE_CERTIFICATE_REVOKED;
					break;
				case SEC_ERROR_UNKNOWN_ISSUER:
				case SEC_ERROR_UNTRUSTED_ISSUER:
					if (!self_signed) {
						*flags |= PURPLE_CERTIFICATE_CA_UNKNOWN;
					}
					break;
				case SEC_ERROR_CA_CERT_INVALID:
				case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
				case SEC_ERROR_UNTRUSTED_CERT:
#ifdef SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED
				case SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED:
#endif
					if (!self_signed) {
						*flags |= PURPLE_CERTIFICATE_INVALID_CHAIN;
					}
					break;
				case SEC_ERROR_BAD_SIGNATURE:
				default:
					*flags |= PURPLE_CERTIFICATE_INVALID_CHAIN;
			}
			if (node->cert)
				CERT_DestroyCertificate(node->cert);
		}
	}

	rv = CERT_VerifyCertName(crt_dat, vrq->subject_name);
	if (rv != SECSuccess) {
		purple_debug_error("nss", "subject name not verified\n");
		*flags |= PURPLE_CERTIFICATE_NAME_MISMATCH;
	}

	PORT_FreeArena(log.arena, PR_FALSE);
}

static PurpleCertificateScheme x509_nss = {
	"x509",                          /* Scheme name */
	N_("X.509 Certificates"),        /* User-visible scheme name */
	x509_import_from_file,           /* Certificate import function */
	x509_export_certificate,         /* Certificate export function */
	x509_copy_certificate,           /* Copy */
	x509_destroy_certificate,        /* Destroy cert */
	x509_signed_by,                  /* Signed-by */
	x509_sha1sum,                    /* SHA1 fingerprint */
	x509_dn,                         /* Unique ID */
	x509_issuer_dn,                  /* Issuer Unique ID */
	x509_common_name,                /* Subject name */
	x509_check_name,                 /* Check subject name */
	x509_times,                      /* Activation/Expiration time */
	x509_importcerts_from_file,      /* Multiple certificate import function */
	x509_register_trusted_tls_cert,  /* Register a certificate as trusted for TLS */
	x509_verify_cert,                /* Verify that the specified cert chain is trusted */
	sizeof(PurpleCertificateScheme), /* struct_size */
	x509_sha256sum,                  /* SHA256 fingerprint */
	NULL,
};

static PurpleSslOps ssl_ops =
{
	ssl_nss_init,
	ssl_nss_uninit,
	ssl_nss_connect,
	ssl_nss_close,
	ssl_nss_read,
	ssl_nss_write,
	ssl_nss_peer_certs,

	/* padding */
	NULL,
	NULL,
	NULL
};


static gboolean
plugin_load(PurplePlugin *plugin)
{
	if (!purple_ssl_get_ops()) {
		purple_ssl_set_ops(&ssl_ops);
	}

	/* Init NSS now, so others can use it even if sslconn never does */
	ssl_nss_init_nss();

	/* Register the X.509 functions we provide */
	purple_certificate_register_scheme(&x509_nss);

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	if (purple_ssl_get_ops() == &ssl_ops) {
		purple_ssl_set_ops(NULL);
	}

	/* Unregister our X.509 functions */
	purple_certificate_unregister_scheme(&x509_nss);

	return TRUE;
}

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,                             /**< type           */
	NULL,                                             /**< ui_requirement */
	PURPLE_PLUGIN_FLAG_INVISIBLE,                       /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                            /**< priority       */

	SSL_NSS_PLUGIN_ID,                             /**< id             */
	N_("NSS"),                                        /**< name           */
	DISPLAY_VERSION,                                  /**< version        */
	                                                  /**  summary        */
	N_("Provides SSL support through Mozilla NSS."),
	                                                  /**  description    */
	N_("Provides SSL support through Mozilla NSS."),
	"Christian Hammond <chipx86@gnupdate.org>",
	PURPLE_WEBSITE,                                     /**< homepage       */

	plugin_load,                                      /**< load           */
	plugin_unload,                                    /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,                                             /**< prefs_info     */
	NULL,                                             /**< actions        */

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
}

PURPLE_INIT_PLUGIN(ssl_nss, init_plugin, info)
