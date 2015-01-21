/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2005 - 2015 Red Hat, Inc.
 * Copyright (C) 2006 - 2008 Novell, Inc.
 */

#ifndef __NETWORKMANAGER_VPN_CONNECTION_H__
#define __NETWORKMANAGER_VPN_CONNECTION_H__

#include <glib.h>
#include <glib-object.h>
#include "nm-vpn-dbus-interface.h"
#include "nm-device.h"
#include "nm-auth-subject.h"
#include "nm-active-connection.h"

#define NM_TYPE_VPN_CONNECTION            (nm_vpn_connection_get_type ())
#define NM_VPN_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_VPN_CONNECTION, NMVpnConnection))
#define NM_VPN_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_VPN_CONNECTION, NMVpnConnectionClass))
#define NM_IS_VPN_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_VPN_CONNECTION))
#define NM_IS_VPN_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_VPN_CONNECTION))
#define NM_VPN_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_VPN_CONNECTION, NMVpnConnectionClass))

/* Properties */
#define NM_VPN_CONNECTION_VPN_STATE "vpn-state"
#define NM_VPN_CONNECTION_BANNER "banner"
#define NM_VPN_CONNECTION_IP_IFACE "ip-iface"
#define NM_VPN_CONNECTION_IP_IFINDEX "ip-ifindex"

/* Signals */
/* not exported: includes old reason code */
#define NM_VPN_CONNECTION_INTERNAL_STATE_CHANGED       "internal-state-changed"
#define NM_VPN_CONNECTION_INTERNAL_RETRY_AFTER_FAILURE "internal-retry-after-failure"


#define NM_VPN_ROUTE_METRIC_DEFAULT     50


struct _NMVpnConnection {
	NMActiveConnection parent;
};

typedef struct {
	NMActiveConnectionClass parent;

	/* Signals */
	void (*vpn_state_changed) (NMVpnConnection *connection,
	                           NMVpnConnectionState new_state,
	                           NMVpnConnectionStateReason reason);

	/* not exported over D-Bus */
	void (*internal_state_changed) (NMVpnConnection *connection,
	                                NMVpnConnectionState new_state,
	                                NMVpnConnectionState old_state,
	                                NMVpnConnectionStateReason reason);

	void (*internal_failed_retry)  (NMVpnConnection *connection);
} NMVpnConnectionClass;

GType nm_vpn_connection_get_type (void);

NMVpnConnection * nm_vpn_connection_new (NMConnection *connection,
                                         NMDevice *parent_device,
                                         const char *specific_object,
                                         NMAuthSubject *subject);

void                 nm_vpn_connection_activate        (NMVpnConnection *connection);
NMConnection *       nm_vpn_connection_get_connection  (NMVpnConnection *connection);
const char*          nm_vpn_connection_get_connection_id (NMVpnConnection *connection);
NMVpnConnectionState nm_vpn_connection_get_vpn_state   (NMVpnConnection *connection);
const char *         nm_vpn_connection_get_banner      (NMVpnConnection *connection);

gboolean             nm_vpn_connection_deactivate      (NMVpnConnection *connection,
                                                        NMVpnConnectionStateReason reason,
                                                        gboolean quitting);
void                 nm_vpn_connection_disconnect      (NMVpnConnection *connection,
                                                        NMVpnConnectionStateReason reason,
                                                        gboolean quitting);

NMIP4Config *        nm_vpn_connection_get_ip4_config  (NMVpnConnection *connection);
NMIP6Config *        nm_vpn_connection_get_ip6_config  (NMVpnConnection *connection);
const char *         nm_vpn_connection_get_ip_iface    (NMVpnConnection *connection);
int                  nm_vpn_connection_get_ip_ifindex  (NMVpnConnection *connection);
guint32              nm_vpn_connection_get_ip4_internal_gateway (NMVpnConnection *connection);
struct in6_addr *    nm_vpn_connection_get_ip6_internal_gateway (NMVpnConnection *connection);

guint32              nm_vpn_connection_get_ip4_route_metric (NMVpnConnection *connection);
guint32              nm_vpn_connection_get_ip6_route_metric (NMVpnConnection *connection);

#endif /* __NETWORKMANAGER_VPN_CONNECTION_H__ */
