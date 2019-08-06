/*
** Zabbix
** Copyright (C) 2001-2019 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

package agent

import (
	"zabbix/pkg/log"
)

type AgentOptions struct {
	LogType             string `conf:",,,console"`
	LogFile             string `conf:",optional"`
	DebugLevel          int    `conf:",,0:5,3"`
	ServerActive        string `conf:",optional"`
	RefreshActiveChecks int    `conf:",,30:3600,120"`
	Timeout             int    `conf:",,1-30,3"`
	Hostname            string
	HostnameItem        string `conf:",optional"`
	HostMetadata        string `conf:",optional"`
	HostMetadataItem    string `conf:",optional"`
	ListenIP            string `conf:",optional"`
	ListenPort          int    `conf:",,1024:32767,10050"`
	Plugins             map[string]map[string]string
}

var Options AgentOptions

func CutAfterN(s string, n int) string {
	var l int

	for i := range s {
		if i > n {
			log.Warningf("value is too long, using first %d characters", l)
			s = s[:l]
			break
		}
		l = i
	}

	return s
}
