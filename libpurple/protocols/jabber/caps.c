/*
 * purple - Jabber Protocol Plugin
 *
 * Copyright (C) 2007, Andreas Monitzer <andy@monitzer.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA	 02111-1307	 USA
 *
 */

#include "internal.h"

#include <glib.h>
#include "caps.h"
#include "cipher.h"
#include <string.h>
#include "internal.h"
#include "util.h"
#include "iq.h"

#define JABBER_CAPS_FILENAME "xmpp-caps.xml"

static GHashTable *capstable = NULL; /* JabberCapsKey -> JabberCapsValue */
static gchar *caps_hash = NULL;

typedef struct _JabberCapsKey {
	char *node;
	char *ver;
} JabberCapsKey;

typedef struct _JabberCapsValueExt {
	GList *identities; /* JabberCapsIdentity */
	GList *features; /* char * */
} JabberCapsValueExt;

typedef struct _JabberCapsValue {
	GList *identities; /* JabberCapsIdentity */
	GList *features; /* char * */
	GHashTable *ext; /* char * -> JabberCapsValueExt */
} JabberCapsValue;

static guint jabber_caps_hash(gconstpointer key) {
	const JabberCapsKey *name = key;
	guint nodehash = g_str_hash(name->node);
	guint verhash = g_str_hash(name->ver);
	
	return nodehash ^ verhash;
}

static gboolean jabber_caps_compare(gconstpointer v1, gconstpointer v2) {
	const JabberCapsKey *name1 = v1;
	const JabberCapsKey *name2 = v2;

	return strcmp(name1->node,name2->node) == 0 && strcmp(name1->ver,name2->ver) == 0;
}

static void jabber_caps_destroy_key(gpointer key) {
	JabberCapsKey *keystruct = key;
	g_free(keystruct->node);
	g_free(keystruct->ver);
	g_free(keystruct);
}

static void jabber_caps_destroy_value(gpointer value) {
	JabberCapsValue *valuestruct = value;
	while(valuestruct->identities) {
		JabberCapsIdentity *id = valuestruct->identities->data;
		g_free(id->category);
		g_free(id->type);
		g_free(id->name);
		g_free(id);
		
		valuestruct->identities = g_list_delete_link(valuestruct->identities,valuestruct->identities);
	}
	while(valuestruct->features) {
		g_free(valuestruct->features->data);
		valuestruct->features = g_list_delete_link(valuestruct->features,valuestruct->features);
	}
	g_hash_table_destroy(valuestruct->ext);
	g_free(valuestruct);
}

static void jabber_caps_ext_destroy_value(gpointer value) {
	JabberCapsValueExt *valuestruct = value;
	while(valuestruct->identities) {
		JabberCapsIdentity *id = valuestruct->identities->data;
		g_free(id->category);
		g_free(id->type);
		g_free(id->name);
		g_free(id);
		
		valuestruct->identities = g_list_delete_link(valuestruct->identities,valuestruct->identities);
	}
	while(valuestruct->features) {
		g_free(valuestruct->features->data);
		valuestruct->features = g_list_delete_link(valuestruct->features,valuestruct->features);
	}
	g_free(valuestruct);
}

static void jabber_caps_load(void);

void jabber_caps_init(void) {
	capstable = g_hash_table_new_full(jabber_caps_hash, jabber_caps_compare, jabber_caps_destroy_key, jabber_caps_destroy_value);
	jabber_caps_load();
	jabber_caps_calculate_own_hash();
}

static void jabber_caps_load(void) {
	xmlnode *capsdata = purple_util_read_xml_from_file(JABBER_CAPS_FILENAME, "XMPP capabilities cache");
	xmlnode *client;

	if(!capsdata)
		return;

	if (strcmp(capsdata->name, "capabilities") != 0) {
		xmlnode_free(capsdata);
		return;
	}
	
	for(client = capsdata->child; client; client = client->next) {
		if(client->type != XMLNODE_TYPE_TAG)
			continue;
		if(!strcmp(client->name, "client")) {
			JabberCapsKey *key = g_new0(JabberCapsKey, 1);
			JabberCapsValue *value = g_new0(JabberCapsValue, 1);
			xmlnode *child;
			key->node = g_strdup(xmlnode_get_attrib(client,"node"));
			key->ver  = g_strdup(xmlnode_get_attrib(client,"ver"));
			value->ext = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, jabber_caps_ext_destroy_value);
			for(child = client->child; child; child = child->next) {
				if(child->type != XMLNODE_TYPE_TAG)
					continue;
				if(!strcmp(child->name,"feature")) {
					const char *var = xmlnode_get_attrib(child, "var");
					if(!var)
						continue;
					value->features = g_list_append(value->features,g_strdup(var));
				} else if(!strcmp(child->name,"identity")) {
					const char *category = xmlnode_get_attrib(child, "category");
					const char *type = xmlnode_get_attrib(child, "type");
					const char *name = xmlnode_get_attrib(child, "name");
					
					JabberCapsIdentity *id = g_new0(JabberCapsIdentity, 1);
					id->category = g_strdup(category);
					id->type = g_strdup(type);
					id->name = g_strdup(name);
					
					value->identities = g_list_append(value->identities,id);
				} else if(!strcmp(child->name,"ext")) {
					const char *identifier = xmlnode_get_attrib(child, "identifier");
					if(identifier) {
						xmlnode *extchild;
						
						JabberCapsValueExt *extvalue = g_new0(JabberCapsValueExt, 1);
						
						for(extchild = child->child; extchild; extchild = extchild->next) {
							if(extchild->type != XMLNODE_TYPE_TAG)
								continue;
							if(!strcmp(extchild->name,"feature")) {
								const char *var = xmlnode_get_attrib(extchild, "var");
								if(!var)
									continue;
								extvalue->features = g_list_append(extvalue->features,g_strdup(var));
							} else if(!strcmp(extchild->name,"identity")) {
								const char *category = xmlnode_get_attrib(extchild, "category");
								const char *type = xmlnode_get_attrib(extchild, "type");
								const char *name = xmlnode_get_attrib(extchild, "name");
								
								JabberCapsIdentity *id = g_new0(JabberCapsIdentity, 1);
								id->category = g_strdup(category);
								id->type = g_strdup(type);
								id->name = g_strdup(name);
								
								extvalue->identities = g_list_append(extvalue->identities,id);
							}
						}
						g_hash_table_replace(value->ext, g_strdup(identifier), extvalue);
					}
				}
			}
			g_hash_table_replace(capstable, key, value);
		}
	}
	xmlnode_free(capsdata);
}

static void jabber_caps_store_ext(gpointer key, gpointer value, gpointer user_data) {
	const char *extname = key;
	JabberCapsValueExt *props = value;
	xmlnode *root = user_data;
	xmlnode *ext = xmlnode_new_child(root,"ext");
	GList *iter;

	xmlnode_set_attrib(ext,"identifier",extname);

	for(iter = props->identities; iter; iter = g_list_next(iter)) {
		JabberCapsIdentity *id = iter->data;
		xmlnode *identity = xmlnode_new_child(ext, "identity");
		xmlnode_set_attrib(identity, "category", id->category);
		xmlnode_set_attrib(identity, "type", id->type);
		if (id->name)
			xmlnode_set_attrib(identity, "name", id->name);
	}

	for(iter = props->features; iter; iter = g_list_next(iter)) {
		const char *feat = iter->data;
		xmlnode *feature = xmlnode_new_child(ext, "feature");
		xmlnode_set_attrib(feature, "var", feat);
	}
}

static void jabber_caps_store_client(gpointer key, gpointer value, gpointer user_data) {
	JabberCapsKey *clientinfo = key;
	JabberCapsValue *props = value;
	xmlnode *root = user_data;
	xmlnode *client = xmlnode_new_child(root,"client");
	GList *iter;

	xmlnode_set_attrib(client,"node",clientinfo->node);
	xmlnode_set_attrib(client,"ver",clientinfo->ver);
	
	for(iter = props->identities; iter; iter = g_list_next(iter)) {
		JabberCapsIdentity *id = iter->data;
		xmlnode *identity = xmlnode_new_child(client, "identity");
		xmlnode_set_attrib(identity, "category", id->category);
		xmlnode_set_attrib(identity, "type", id->type);
		if (id->name)
			xmlnode_set_attrib(identity, "name", id->name);
	}

	for(iter = props->features; iter; iter = g_list_next(iter)) {
		const char *feat = iter->data;
		xmlnode *feature = xmlnode_new_child(client, "feature");
		xmlnode_set_attrib(feature, "var", feat);
	}
	
	g_hash_table_foreach(props->ext,jabber_caps_store_ext,client);
}

static void jabber_caps_store(void) {
	char *str;
	xmlnode *root = xmlnode_new("capabilities");
	g_hash_table_foreach(capstable, jabber_caps_store_client, root);
	str = xmlnode_to_formatted_str(root, NULL);
	xmlnode_free(root);
	purple_util_write_data_to_file(JABBER_CAPS_FILENAME, str, -1);
	g_free(str);
}

/* this function assumes that all information is available locally */
static JabberCapsClientInfo *jabber_caps_collect_info(const char *node, const char *ver, GList *ext) {
	JabberCapsClientInfo *result;
	JabberCapsKey *key = g_new0(JabberCapsKey, 1);
	JabberCapsValue *caps;
	GList *iter;

	key->node = (char *)node;
	key->ver = (char *)ver;

	caps = g_hash_table_lookup(capstable,key);

	g_free(key);

	if (caps == NULL)
		return NULL;

	result = g_new0(JabberCapsClientInfo, 1);

	/* join all information */
	for(iter = caps->identities; iter; iter = g_list_next(iter)) {
		JabberCapsIdentity *id = iter->data;
		JabberCapsIdentity *newid = g_new0(JabberCapsIdentity, 1);
		newid->category = g_strdup(id->category);
		newid->type = g_strdup(id->type);
		newid->name = g_strdup(id->name);
		
		result->identities = g_list_append(result->identities,newid);
	}
	for(iter = caps->features; iter; iter = g_list_next(iter)) {
		const char *feat = iter->data;
		char *newfeat = g_strdup(feat);
		
		result->features = g_list_append(result->features,newfeat);
	}
	
	for(iter = ext; iter; iter = g_list_next(iter)) {
		const char *extname = iter->data;
		JabberCapsValueExt *extinfo = g_hash_table_lookup(caps->ext,extname);
		
		if(extinfo) {
			GList *iter2;
			for(iter2 = extinfo->identities; iter2; iter2 = g_list_next(iter2)) {
				JabberCapsIdentity *id = iter2->data;
				JabberCapsIdentity *newid = g_new0(JabberCapsIdentity, 1);
				newid->category = g_strdup(id->category);
				newid->type = g_strdup(id->type);
				newid->name = g_strdup(id->name);
				
				result->identities = g_list_append(result->identities,newid);
			}
			for(iter2 = extinfo->features; iter2; iter2 = g_list_next(iter2)) {
				const char *feat = iter2->data;
				char *newfeat = g_strdup(feat);
				
				result->features = g_list_append(result->features,newfeat);
			}
		}
	}
	return result;
}

void jabber_caps_free_clientinfo(JabberCapsClientInfo *clientinfo) {
	if(!clientinfo)
		return;
	while(clientinfo->identities) {
		JabberCapsIdentity *id = clientinfo->identities->data;
		g_free(id->category);
		g_free(id->type);
		g_free(id->name);
		g_free(id);
		
		clientinfo->identities = g_list_delete_link(clientinfo->identities,clientinfo->identities);
	}
	while(clientinfo->features) {
		char *feat = clientinfo->features->data;
		g_free(feat);
		
		clientinfo->features = g_list_delete_link(clientinfo->features,clientinfo->features);
	}
	
	g_free(clientinfo);
}

typedef struct _jabber_caps_cbplususerdata {
	jabber_caps_get_info_cb cb;
	gpointer user_data;
	
	char *who;
	char *node;
	char *ver;
	GList *ext;
	unsigned extOutstanding;
} jabber_caps_cbplususerdata;

typedef struct jabber_ext_userdata {
	jabber_caps_cbplususerdata *userdata;
	char *node;
} jabber_ext_userdata;

static void jabber_caps_get_info_check_completion(jabber_caps_cbplususerdata *userdata) {
	if(userdata->extOutstanding == 0) {
		userdata->cb(jabber_caps_collect_info(userdata->node, userdata->ver, userdata->ext), userdata->user_data);
		g_free(userdata->who);
		g_free(userdata->node);
		g_free(userdata->ver);
		while(userdata->ext) {
			g_free(userdata->ext->data);
			userdata->ext = g_list_delete_link(userdata->ext,userdata->ext);
		}
		g_free(userdata);
	}
}

static void jabber_caps_ext_iqcb(JabberStream *js, xmlnode *packet, gpointer data) {
	/* collect data and fetch all exts */
	xmlnode *query = xmlnode_get_child_with_namespace(packet, "query", "http://jabber.org/protocol/disco#info");
	jabber_ext_userdata *extuserdata = data;
	jabber_caps_cbplususerdata *userdata = extuserdata->userdata;
	const char *node = extuserdata->node;

	--userdata->extOutstanding;

	/* TODO: Better error handling */

	if(node && query) {
		const char *key;
		JabberCapsValue *client;
		xmlnode *child;
		JabberCapsValueExt *value = g_new0(JabberCapsValueExt, 1);
		JabberCapsKey *clientkey = g_new0(JabberCapsKey, 1);

		clientkey->node = userdata->node;
		clientkey->ver = userdata->ver;

		client = g_hash_table_lookup(capstable, clientkey);

		g_free(clientkey);

		/* split node by #, key either points to \0 or the correct ext afterwards */
		for(key = node; key[0] != '\0'; ++key) {
			if(key[0] == '#') {
				++key;
				break;
			}
		}

		for(child = query->child; child; child = child->next) {
			if(child->type != XMLNODE_TYPE_TAG)
				continue;
			if(!strcmp(child->name,"feature")) {
				const char *var = xmlnode_get_attrib(child, "var");
				if(!var)
					continue;
				value->features = g_list_append(value->features,g_strdup(var));
			} else if(!strcmp(child->name,"identity")) {
				const char *category = xmlnode_get_attrib(child, "category");
				const char *type = xmlnode_get_attrib(child, "type");
				const char *name = xmlnode_get_attrib(child, "name");

				JabberCapsIdentity *id = g_new0(JabberCapsIdentity, 1);
				id->category = g_strdup(category);
				id->type = g_strdup(type);
				id->name = g_strdup(name);

				value->identities = g_list_append(value->identities,id);
			}
		}
		g_hash_table_replace(client->ext, g_strdup(key), value);

		jabber_caps_store();
	}

	g_free(extuserdata->node);
	g_free(extuserdata);
	jabber_caps_get_info_check_completion(userdata);
}

static void jabber_caps_client_iqcb(JabberStream *js, xmlnode *packet, gpointer data) {
	/* collect data and fetch all exts */
	xmlnode *query = xmlnode_get_child_with_namespace(packet, "query",
		"http://jabber.org/protocol/disco#info");
	xmlnode *child;
	GList *iter;
	jabber_caps_cbplususerdata *userdata = data;

	/* TODO: Better error checking! */

	if (query) {
		
		JabberCapsValue *value = g_new0(JabberCapsValue, 1);
		JabberCapsKey *key = g_new0(JabberCapsKey, 1);

		value->ext = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, jabber_caps_ext_destroy_value);

		key->node = g_strdup(userdata->node);
		key->ver = g_strdup(userdata->ver);

		for(child = query->child; child; child = child->next) {
			if(child->type != XMLNODE_TYPE_TAG)
				continue;
			if(!strcmp(child->name,"feature")) {
				const char *var = xmlnode_get_attrib(child, "var");
				if(!var)
					continue;
				value->features = g_list_append(value->features, g_strdup(var));
			} else if(!strcmp(child->name,"identity")) {
				const char *category = xmlnode_get_attrib(child, "category");
				const char *type = xmlnode_get_attrib(child, "type");
				const char *name = xmlnode_get_attrib(child, "name");

				JabberCapsIdentity *id = g_new0(JabberCapsIdentity, 1);
				id->category = g_strdup(category);
				id->type = g_strdup(type);
				id->name = g_strdup(name);

				value->identities = g_list_append(value->identities,id);
			}
		}
		g_hash_table_replace(capstable, key, value);
		jabber_caps_store();
	}

	/* fetch all exts */
	for(iter = userdata->ext; iter; iter = g_list_next(iter)) {
		JabberIq *iq = jabber_iq_new_query(js, JABBER_IQ_GET, "http://jabber.org/protocol/disco#info");
		xmlnode *query = xmlnode_get_child_with_namespace(iq->node, "query", "http://jabber.org/protocol/disco#info");
		char *node = g_strdup_printf("%s#%s", userdata->node, (const char*)iter->data);
		jabber_ext_userdata *ext_data = g_new0(jabber_ext_userdata, 1);
		ext_data->node = node;
		ext_data->userdata = userdata;

		xmlnode_set_attrib(query, "node", node);
		xmlnode_set_attrib(iq->node, "to", userdata->who);

		jabber_iq_set_callback(iq, jabber_caps_ext_iqcb, ext_data);
		jabber_iq_send(iq);
	}

	jabber_caps_get_info_check_completion(userdata);
}

void jabber_caps_get_info(JabberStream *js, const char *who, const char *node, const char *ver, const char *ext, jabber_caps_get_info_cb cb, gpointer user_data) {
	JabberCapsValue *client;
	JabberCapsKey *key = g_new0(JabberCapsKey, 1);
	jabber_caps_cbplususerdata *userdata = g_new0(jabber_caps_cbplususerdata, 1);
	userdata->cb = cb;
	userdata->user_data = user_data;
	userdata->who = g_strdup(who);
	userdata->node = g_strdup(node);
	userdata->ver = g_strdup(ver);

	key->node = (char *)node;
	key->ver = (char *)ver;
	
	client = g_hash_table_lookup(capstable, key);

	g_free(key);

//	if(!client) {
		JabberIq *iq = jabber_iq_new_query(js,JABBER_IQ_GET,"http://jabber.org/protocol/disco#info");
		xmlnode *query = xmlnode_get_child_with_namespace(iq->node,"query","http://jabber.org/protocol/disco#info");
		char *nodever = g_strdup_printf("%s#%s", node, ver);
		xmlnode_set_attrib(query, "node", nodever);
		g_free(nodever);
		xmlnode_set_attrib(iq->node, "to", who);

		jabber_iq_set_callback(iq,jabber_caps_client_iqcb,userdata);
		jabber_iq_send(iq);
#if 0
	} else {
		GList *iter;
		/* fetch unknown exts only */
		for(iter = userdata->ext; iter; iter = g_list_next(iter)) {
			JabberCapsValueExt *extvalue = g_hash_table_lookup(client->ext, (const char*)iter->data);
			JabberIq *iq;
			xmlnode *query;
			char *nodever;
			jabber_ext_userdata *ext_data;

			if(extvalue) {
				/* we already have this ext, don't bother with it */
				--userdata->extOutstanding;
				continue;
			}

			ext_data = g_new0(jabber_ext_userdata, 1);

			iq = jabber_iq_new_query(js,JABBER_IQ_GET,"http://jabber.org/protocol/disco#info");
			query = xmlnode_get_child_with_namespace(iq->node,"query","http://jabber.org/protocol/disco#info");
			nodever = g_strdup_printf("%s#%s", node, (const char*)iter->data);
			xmlnode_set_attrib(query, "node", nodever);
			xmlnode_set_attrib(iq->node, "to", who);

			ext_data->node = nodever;
			ext_data->userdata = userdata;

			jabber_iq_set_callback(iq, jabber_caps_ext_iqcb, ext_data);
			jabber_iq_send(iq);
		}
		/* maybe we have all data available anyways? This is the ideal case where no network traffic is necessary */
		jabber_caps_get_info_check_completion(userdata);
	}
#endif
}

static gint jabber_caps_jabber_identity_compare(gconstpointer a, gconstpointer b) {
	const JabberIdentity *ac;
	const JabberIdentity *bc;
	gint cat_cmp;
	gint typ_cmp;
	
	ac = a;
	bc = b;
	
	if ((cat_cmp = strcmp(ac->category, bc->category)) == 0) {
		if ((typ_cmp = strcmp(ac->type, bc->type)) == 0) {
			return strcmp(ac->lang, bc->lang);
		} else {
			return typ_cmp;
		}
	} else {
		return cat_cmp;
	}
}

static gint jabber_caps_jabber_feature_compare(gconstpointer a, gconstpointer b) {
	const JabberFeature *ac;
	const JabberFeature *bc;
	
	ac = a;
	bc = b;
	
	return strcmp(ac->namespace, bc->namespace);
}

static gint jabber_caps_string_compare(gconstpointer a, gconstpointer b) {
	const gchar *ac;
	const gchar *bc;
	
	ac = a;
	bc = b;
	
	return strcmp(ac, bc);
}

static gint jabber_caps_jabber_xdata_compare(gconstpointer a, gconstpointer b) {
	const xmlnode *ac;
	const xmlnode *bc;
	xmlnode *aformtypefield;
	xmlnode *bformtypefield;
	char *aformtype;
	char *bformtype;
	int result;
	
	ac = a;
	bc = b;

	aformtypefield = xmlnode_get_child(ac, "field");
	while (aformtypefield && strcmp(xmlnode_get_attrib(aformtypefield, "var"), "FORM_TYPE")) aformtypefield = xmlnode_get_next_twin(aformtypefield);
	aformtype = xmlnode_get_data(aformtypefield);
	
	bformtypefield = xmlnode_get_child(bc, "field");
	while (bformtypefield && strcmp(xmlnode_get_attrib(bformtypefield, "var"), "FORM_TYPE")) bformtypefield = xmlnode_get_next_twin(bformtypefield);
	bformtype = xmlnode_get_data(bformtypefield);
	
	result = strcmp(aformtype, bformtype);
	g_free(aformtype);
	g_free(bformtype);
	return result;
}

JabberCapsClientInfo *jabber_caps_parse_client_info(xmlnode *query) {
	xmlnode *child;
	
	if (!query) return 0;
	if (strcmp(query->xmlns,"http://jabber.org/protocol/disco#info")) return 0;
	
	JabberCapsClientInfo *info = g_new0(JabberCapsClientInfo, 1);
	
	for(child = query->child; child; child = child->next) {
		if (!strcmp(child->name,"identity")) {
			/* parse identity */
			const char *category = xmlnode_get_attrib(child, "category");
			const char *type = xmlnode_get_attrib(child, "type");
			const char *name = xmlnode_get_attrib(child, "name");

			JabberCapsIdentity *id = g_new0(JabberCapsIdentity, 1);
			id->category = g_strdup(category);
			id->type = g_strdup(type);
			id->name = g_strdup(name);
			
			info->identities = g_list_append(info->identities, id);
		} else if (!strcmp(child->name, "feature")) {
			/* parse feature */
			const char *var = xmlnode_get_attrib(child, "var");
			if(!var)
				continue;
			info->features = g_list_append(info->features, g_strdup(var));
		} else if (!strcmp(child->name, "x")) {
			if (!strcmp(child->xmlns, "jabber:x:data")) {
				/* x-data form */
				xmlnode *dataform = xmlnode_copy(child);
				info->forms = g_list_append(info->forms, dataform);
			}
		}
	}
	return info;
}

gchar *jabber_caps_calcualte_hash(JabberCapsClientInfo *info) {
	if (!info) return 0;
	
	/* sort identities, features and x-data forms */
	info->identities = g_list_sort(info->identities, jabber_caps_jabber_identity_compare);
	info->features = g_list_sort(info->features, jabber_caps_string_compare);
	info->forms = g_list_sort(info->forms, jabber_caps_jabber_xdata_compare);
}

void jabber_caps_calculate_own_hash() {
	gchar *verification = 0;
	gchar *free_verification;
	gchar *identity_string, *feature_string;
	GList *identities, *features;
	PurpleCipherContext *context;
	guint8 checksum[20];
	gsize checksum_size = 20;

	/* sort identities */
	jabber_identities = g_list_sort(jabber_identities, jabber_caps_jabber_identity_compare);
	
	/* concat identities to the verification string */
	for(identities = jabber_identities; identities; identities = identities->next) {
		JabberIdentity *ident = (JabberIdentity*)identities->data;
		identity_string = g_strdup_printf("%s/%s//%s<", ident->category, ident->type, ident->name);
		free_verification = verification;
		if(verification == 0) verification = g_strdup(identity_string);
		 	else verification = g_strconcat(verification, identity_string, NULL);
		g_free(identity_string);
		if(free_verification) g_free(free_verification);
	}
	
	/* sort features */
	jabber_features = g_list_sort(jabber_features, jabber_caps_jabber_feature_compare);
	
	/* concat features to the verification string */
	for(features = jabber_features; features; features = features->next) {
		JabberFeature *feat = (JabberFeature*)features->data;
		feature_string = g_strdup_printf("%s<", feat->namespace);
		free_verification = verification;
		if(verification == 0) g_strdup(feature_string);
			else verification = g_strconcat(verification, feature_string, NULL);
		g_free(feature_string);
		if(free_verification) g_free(free_verification);
	}
	printf("\n%s", verification);
	
	
	/* generate SHA-1 hash */
	context = purple_cipher_context_new_by_name("sha1", NULL);
	if (context == NULL) {
		purple_debug_error("jabber", "Could not find sha1 cipher\n");
		return;
	}
	purple_cipher_context_append(context, verification, strlen(verification));
	
	if (!purple_cipher_context_digest(context, strlen(verification), checksum, &checksum_size)) {
		purple_debug_error("util", "Failed to get SHA-1 digest.\n");
	}
	purple_cipher_context_destroy(context);
	
	/* apply Base64 on hash */
	
	g_free(verification);
	verification = purple_base64_encode(checksum, checksum_size); // for 2.0 compability
	printf("\n%s", verification);
	
	if (caps_hash != 0) g_free(caps_hash);
	caps_hash = verification;
}

const gchar* jabber_caps_get_own_hash() {
	return caps_hash;
}

