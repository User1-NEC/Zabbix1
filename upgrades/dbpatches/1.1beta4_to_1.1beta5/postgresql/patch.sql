alter table screens_items  add elements		int4		DEFAULT '25' NOT NULL;
alter table actions add  source			int2		DEFAULT '0' NOT NULL;
alter table actions add  actiontype		int2		DEFAULT '0' NOT NULL;
alter table actions add  filter_triggerid	int4		DEFAULT '0' NOT NULL;
alter table actions add  filter_hostid		int4		DEFAULT '0' NOT NULL;
alter table actions add  filter_groupid	int4		DEFAULT '0' NOT NULL;
alter table actions add  filter_trigger_name	varchar(255)	DEFAULT '' NOT NULL;
update actions set filter_triggerid=triggerid where scope=0;
update actions set filter_hostid=triggerid where scope=1;
alter table actions drop triggerid;
alter table actions drop scope;
alter table actions drop good;
