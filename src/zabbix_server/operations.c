/*
** ZABBIX
** Copyright (C) 2000-2005 SIA Zabbix
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**/

#include "common.h"
#include "comms.h"
#include "db.h"
#include "log.h"

#include "operations.h"

/******************************************************************************
 *                                                                            *
 * Function: select_discovered_host                                           *
 *                                                                            *
 * Purpose: select hostid of discovered host                                  *
 *                                                                            *
 * Parameters: dhostid - discovered host id                                   *
 *                                                                            *
 * Return value: hostid - existing hostid, 0 - if not found                   *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static zbx_uint64_t	select_discovered_host(DB_EVENT *event)
{
	const char	*__function_name = "select_discovered_host";
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	hostid = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s(eventid:" ZBX_FS_UI64 ")",
			__function_name, event->eventid);

	switch (event->object) {
	case EVENT_OBJECT_DHOST:
		result = DBselect(
				"select h.hostid"
				" from hosts h,interface i,dservices ds"
				" where h.hostid=i.hostid"
					" and i.useip=1"
					" and i.ip=ds.ip"
					" and ds.dhostid=" ZBX_FS_UI64,
				event->objectid);
		break;
	case EVENT_OBJECT_DSERVICE:
		result = DBselect(
				"select h.hostid"
				" from hosts h,interface i,dservices ds"
				" where h.hostid=i.hostid"
					" and i.useip=1"
					" and i.ip=ds.ip"
					" and ds.dserviceid =" ZBX_FS_UI64,
				event->objectid);
		break;
	case EVENT_OBJECT_ZABBIX_ACTIVE:
		result = DBselect(
				"select h.hostid"
				" from hosts h,autoreg_host a"
				" where a.proxy_hostid=h.proxy_hostid"
					" and a.host=h.host"
					" and a.autoreg_hostid=" ZBX_FS_UI64,
				event->objectid);
		break;
	default:
		return 0;
	}

	if (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(hostid, row[0]);
	}
	DBfree_result(result);

	zabbix_log(LOG_LEVEL_DEBUG, "End %s()", __function_name);

	return hostid;
}

/******************************************************************************
 *                                                                            *
 * Function: add_discovered_host_group                                        *
 *                                                                            *
 * Purpose: add group to host if not added already                            *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void	add_discovered_host_group(zbx_uint64_t hostid, zbx_uint64_t groupid)
{
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	hostgroupid;

	result = DBselect(
			"select hostgroupid"
			" from hosts_groups"
			" where groupid=" ZBX_FS_UI64
				" and hostid=" ZBX_FS_UI64,
			groupid,
			hostid);

	if (NULL == (row = DBfetch(result)))
	{
		hostgroupid = DBget_maxid("hosts_groups");
		DBexecute("insert into hosts_groups (hostgroupid,hostid,groupid)"
				" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64 ")",
				hostgroupid,
				hostid,
				groupid);
	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: add_discovered_host                                              *
 *                                                                            *
 * Purpose: add discovered host if it was not added already                   *
 *                                                                            *
 * Parameters: dhostid - discovered host id                                   *
 *                                                                            *
 * Return value: hostid - new/existing hostid                                 *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static zbx_uint64_t	add_discovered_host(DB_EVENT *event)
{
	const char	*__function_name = "add_discovered_host";
	DB_RESULT	result;
	DB_RESULT	result2;
	DB_ROW		row;
	DB_ROW		row2;
	zbx_uint64_t	dhostid, hostid = 0, proxy_hostid, host_proxy_hostid;
	char		host[MAX_STRING_LEN], *host_esc, *host_unique, *host_unique_esc;
	unsigned short	port;
	zbx_uint64_t	groupid = 0;
	unsigned char	svc_type, interface_type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s(eventid:" ZBX_FS_UI64 ")",
			__function_name, event->eventid);

	result = DBselect(
			"select discovery_groupid"
			" from config"
			" where 1=1"
				DB_NODE,
			DBnode_local("configid"));

	if (NULL != (row = DBfetch(result)))
		ZBX_STR2UINT64(groupid, row[0]);
	DBfree_result(result);

	if (0 == groupid)
	{
		zabbix_log(LOG_LEVEL_WARNING, "Can't add discovered host:"
				" Group for discovered hosts is not defined");
		return hostid;
	}

	if (EVENT_OBJECT_DHOST == event->object || EVENT_OBJECT_DSERVICE == event->object)
	{
		if (EVENT_OBJECT_DHOST == event->object)
		{
			result = DBselect(
					"select ds.dhostid,dr.proxy_hostid,ds.ip,ds.port,ds.type"
					" from drules dr,dchecks dc,dservices ds"
					" where dc.druleid=dr.druleid"
						" and ds.dcheckid=dc.dcheckid"
						" and ds.dhostid=" ZBX_FS_UI64
					" order by ds.dserviceid",
					event->objectid);
		}
		else
		{
			result = DBselect(
					"select ds.dhostid,dr.proxy_hostid,ds.ip,ds.port,ds.type"
					" from drules dr,dchecks dc,dservices ds,dservices ds1"
					" where dc.druleid=dr.druleid"
						" and ds.dcheckid=dc.dcheckid"
						" and ds1.dhostid=ds.dhostid"
						" and ds1.dserviceid=" ZBX_FS_UI64
					" order by ds.dserviceid",
					event->objectid);
		}

		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(dhostid, row[0]);
			ZBX_DBROW2UINT64(proxy_hostid, row[1]);
			svc_type = (unsigned char)atoi(row[4]);

			switch (svc_type)
			{
				case SVC_AGENT:
					port = (unsigned short)atoi(row[3]);
					interface_type = INTERFACE_TYPE_AGENT;
					break;
				case SVC_SNMPv1:
				case SVC_SNMPv2c:
				case SVC_SNMPv3:
					port = (unsigned short)atoi(row[3]);
					interface_type = INTERFACE_TYPE_SNMP;
					break;
				default:
					port = ZBX_DEFAULT_AGENT_PORT;
					interface_type = INTERFACE_TYPE_AGENT;
			}

			alarm(CONFIG_TIMEOUT);
			zbx_gethost_by_ip(row[2], host, sizeof(host));
			alarm(0);

			if (0 == hostid)
			{
				result2 = DBselect(
						"select distinct h.hostid,h.proxy_hostid"
						" from hosts h,interface i,dservices ds"
						" where h.hostid=i.hostid"
							" and i.ip=ds.ip"
							" and ds.dhostid=" ZBX_FS_UI64
						       	DB_NODE
						" order by h.hostid",
						dhostid,
						DBnode_local("h.hostid"));

				if (NULL != (row2 = DBfetch(result2)))
				{
					ZBX_STR2UINT64(hostid, row2[0]);
					ZBX_DBROW2UINT64(host_proxy_hostid, row2[1]);
				}
				DBfree_result(result2);
			}

			if (0 == hostid)
			{
				hostid = DBget_maxid("hosts");

				/* for host uniqueness purposes */
				if ('\0' != *host)
				{
					/* by host name */
					make_hostname(host); /* replace not-allowed symbols */
					host_unique = DBget_unique_hostname_by_sample(host);
				}
				else
				{
					/* by ip */
					make_hostname(row[2]); /* replace not-allowed symbols */
					host_unique = DBget_unique_hostname_by_sample(row[2]);
				}				

				host_unique_esc = DBdyn_escape_string(host_unique);

				DBexecute("insert into hosts"
							" (hostid,proxy_hostid,host)"
						" values"
							" (" ZBX_FS_UI64 ",%s,'%s')",
						hostid, DBsql_id_ins(proxy_hostid), host_unique_esc);

				DBadd_interface(hostid, interface_type, 1, row[2], host, port);

				zbx_free(host_unique);
				zbx_free(host_unique_esc);
			}
			else
			{
				if (host_proxy_hostid != proxy_hostid)
				{
					DBexecute("update hosts"
							" set proxy_hostid=%s"
							" where hostid=" ZBX_FS_UI64,
							DBsql_id_ins(proxy_hostid),
							hostid);
				}

				DBadd_interface(hostid, interface_type, 1, row[2], host, port);
			}
		}
		DBfree_result(result);
	}
	else if (EVENT_OBJECT_ZABBIX_ACTIVE == event->object)
	{
		result = DBselect(
				"select proxy_hostid,host,listen_ip,listen_port"
				" from autoreg_host"
				" where autoreg_hostid=" ZBX_FS_UI64,
				event->objectid);

		if (NULL != (row = DBfetch(result)))
		{
			ZBX_DBROW2UINT64(proxy_hostid, row[0]);
			host_esc = DBdyn_escape_string_len(row[1], HOST_HOST_LEN);
			port = (unsigned short)atoi(row[3]);

			result2 = DBselect(
					"select hostid,proxy_hostid"
					" from hosts"
					" where host='%s'"
						DB_NODE,
					host_esc,
					DBnode_local("hostid"));

			if (NULL == (row2 = DBfetch(result2)))
			{
				hostid = DBget_maxid("hosts");

				DBexecute("insert into hosts"
							" (hostid,proxy_hostid,host)"
						" values"
							" (" ZBX_FS_UI64 ",%s,'%s')",
						hostid, DBsql_id_ins(proxy_hostid), host_esc);

				DBadd_interface(hostid, INTERFACE_TYPE_AGENT, 1, row[2], row[1], port);
			}
			else
			{
				ZBX_STR2UINT64(hostid, row2[0]);
				ZBX_DBROW2UINT64(host_proxy_hostid, row2[1]);

				if (host_proxy_hostid != proxy_hostid)
				{
					DBexecute("update hosts"
							" set proxy_hostid=%s"
							" where hostid=" ZBX_FS_UI64,
							DBsql_id_ins(proxy_hostid),
							hostid);
				}

				DBadd_interface(hostid, INTERFACE_TYPE_AGENT, 1, row[2], row[1], port);
			}
			DBfree_result(result2);

			zbx_free(host_esc);
		}
		DBfree_result(result);
	}

	if (0 != hostid)
		add_discovered_host_group(hostid, groupid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return hostid;
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_add                                                      *
 *                                                                            *
 * Purpose: add discovered host                                               *
 *                                                                            *
 * Parameters: trigger - trigger data                                         *
 *             action  - action data                                          *
 *                                                                            *
 * Return value: nothing                                                      *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	op_host_add(DB_EVENT *event)
{
	const char	*__function_name = "op_host_add";

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (event->source != EVENT_SOURCE_DISCOVERY && event->source != EVENT_SOURCE_AUTO_REGISTRATION)
		return;

	if (event->object != EVENT_OBJECT_DHOST && event->object != EVENT_OBJECT_DSERVICE && event->object != EVENT_OBJECT_ZABBIX_ACTIVE)
		return;

	add_discovered_host(event);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_del                                                      *
 *                                                                            *
 * Purpose: delete host                                                       *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: nothing                                                      *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	op_host_del(DB_EVENT *event)
{
	const char	*__function_name = "op_host_del";
	zbx_uint64_t	hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (event->source != EVENT_SOURCE_DISCOVERY)
		return;

	if (event->object != EVENT_OBJECT_DHOST && event->object != EVENT_OBJECT_DSERVICE)
		return;

	if (0 == (hostid = select_discovered_host(event)))
		return;

	DBdelete_host(hostid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_enable                                                   *
 *                                                                            *
 * Purpose: enable discovered                                                 *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: nothing                                                      *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	op_host_enable(DB_EVENT *event)
{
	const char	*__function_name = "op_host_enable";
	zbx_uint64_t	hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (event->source != EVENT_SOURCE_DISCOVERY)
		return;

	if (event->object != EVENT_OBJECT_DHOST && event->object != EVENT_OBJECT_DSERVICE)
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	DBexecute(
			"update hosts"
			" set status=%d"
			" where hostid=" ZBX_FS_UI64,
			HOST_STATUS_MONITORED,
			hostid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_disable                                                  *
 *                                                                            *
 * Purpose: disable host                                                      *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: nothing                                                      *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	op_host_disable(DB_EVENT *event)
{
	const char	*__function_name = "op_host_disable";
	zbx_uint64_t	hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (event->source != EVENT_SOURCE_DISCOVERY && event->source != EVENT_SOURCE_AUTO_REGISTRATION)
		return;

	if (event->object != EVENT_OBJECT_DHOST && event->object != EVENT_OBJECT_DSERVICE && event->object != EVENT_OBJECT_ZABBIX_ACTIVE)
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	DBexecute(
			"update hosts"
			" set status=%d"
			" where hostid=" ZBX_FS_UI64,
			HOST_STATUS_NOT_MONITORED,
			hostid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_group_add                                                     *
 *                                                                            *
 * Purpose: add group to discovered host                                      *
 *                                                                            *
 * Parameters: event   - [IN] event data                                      *
 *             groupid - [IN] group identificator from database               *
 *                                                                            *
 * Return value: nothing                                                      *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	op_group_add(DB_EVENT *event, zbx_uint64_t groupid)
{
	const char	*__function_name = "op_group_add";
	zbx_uint64_t	hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (event->source != EVENT_SOURCE_DISCOVERY && event->source != EVENT_SOURCE_AUTO_REGISTRATION)
		return;

	if (event->object != EVENT_OBJECT_DHOST && event->object != EVENT_OBJECT_DSERVICE && event->object != EVENT_OBJECT_ZABBIX_ACTIVE)
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	add_discovered_host_group(hostid, groupid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_group_del                                                     *
 *                                                                            *
 * Purpose: delete group from discovered host                                 *
 *                                                                            *
 * Parameters: event   - [IN] event data                                      *
 *             groupid - [IN] group identificator from database               *
 *                                                                            *
 * Return value: nothing                                                      *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	op_group_del(DB_EVENT *event, zbx_uint64_t groupid)
{
	const char	*__function_name = "op_group_del";
	zbx_uint64_t	hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (event->source != EVENT_SOURCE_DISCOVERY)
		return;

	if (event->object != EVENT_OBJECT_DHOST && event->object != EVENT_OBJECT_DSERVICE)
		return;

	if (0 == (hostid = select_discovered_host(event)))
		return;

	DBexecute(
			"delete from hosts_groups"
			" where hostid=" ZBX_FS_UI64
				" and groupid=" ZBX_FS_UI64,
			hostid,
			groupid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_template_add                                                  *
 *                                                                            *
 * Purpose: link host with template                                           *
 *                                                                            *
 * Parameters: event      - [IN] event data                                   *
 *             templateid - [IN] host template identificator from database    *
 *                                                                            *
 * Return value: nothing                                                      *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	op_template_add(DB_EVENT *event, zbx_uint64_t templateid)
{
	const char	*__function_name = "op_template_add";
	zbx_uint64_t	hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (event->source != EVENT_SOURCE_DISCOVERY && event->source != EVENT_SOURCE_AUTO_REGISTRATION)
		return;

	if (event->object != EVENT_OBJECT_DHOST && event->object != EVENT_OBJECT_DSERVICE && event->object != EVENT_OBJECT_ZABBIX_ACTIVE)
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	DBcopy_template_elements(hostid, templateid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: op_template_del                                                  *
 *                                                                            *
 * Purpose: unlink and clear host from template                               *
 *                                                                            *
 * Parameters: event      - [IN] event data                                   *
 *             templateid - [IN] host template identificator from database    *
 *                                                                            *
 * Return value: nothing                                                      *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
void	op_template_del(DB_EVENT *event, zbx_uint64_t templateid)
{
	const char	*__function_name = "op_template_del";
	zbx_uint64_t	hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (event->source != EVENT_SOURCE_DISCOVERY)
		return;

	if (event->object != EVENT_OBJECT_DHOST && event->object != EVENT_OBJECT_DSERVICE)
		return;

	if (0 == (hostid = select_discovered_host(event)))
		return;

	DBdelete_template_elements(hostid, templateid);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
