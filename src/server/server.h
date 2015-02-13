/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
 * Copyright (c) 2014 Tyler Watson
 * Copyright (c) 2009 Andrey Litvinov
 *
 * This file is part of oBIX.
 *
 * oBIX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * oBIX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with oBIX. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#ifndef OBIX_SERVER_H_
#define OBIX_SERVER_H_

#include <libxml/tree.h>
#include "xml_config.h"
#include "obix_request.h"

int obix_server_init(const xml_config_t *);
void obix_server_exit(void);

xmlNode *obix_server_generate_error(const xmlChar *href, const char *contract,
									const char *name, const char *desc);

void obix_server_handleError(obix_request_t *request, const char *msg);
void obix_server_handleGET(obix_request_t *request);
void obix_server_handlePUT(obix_request_t *request, const xmlDoc *input);
void obix_server_handlePOST(obix_request_t *request, const xmlDoc *input);

xmlNode *obix_server_read(obix_request_t *request, const xmlChar *overrideUri);
xmlNode *obix_server_write(obix_request_t *request, const xmlChar *overrideUri,
						   xmlNode *input);
xmlNode *obix_server_invoke(obix_request_t *request, const xmlChar *overrideUri,
							xmlNode *input);

void obix_server_remove_meta(xmlNode *obixObject);
void obix_server_reply_object(obix_request_t *request, xmlNode *obixObject);

#endif /* OBIX_SERVER_H_ */
