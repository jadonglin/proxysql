#include <map>
#include <mutex>
//#include <thread>
#include "proxysql.h"
#include "cpp.h"

//#include "thread.h"
//#include "wqueue.h"

#include <fcntl.h>
#include <sys/times.h>

#ifdef DEBUG
#define DEB "_DEBUG"
#else
#define DEB ""
#endif /* DEBUG */
#define PROXYSQL_STATISTICS_VERSION "1.4.1027" DEB

extern ProxySQL_Admin *GloAdmin;
extern MySQL_Threads_Handler *GloMTH;


#define SAFE_SQLITE3_STEP(_stmt) do {\
	do {\
		rc=sqlite3_step(_stmt);\
		if (rc!=SQLITE_DONE) {\
			assert(rc==SQLITE_LOCKED)\
			usleep(100);\
		}\
	} while (rc!=SQLITE_DONE );\
} while (0)

#define SAFE_SQLITE3_STEP2(_stmt) do {\
	do {\
		rc=sqlite3_step(_stmt);\
		if (rc==SQLITE_LOCKED || rc==SQLITE_BUSY) {\
			usleep(100);\
		}\
	} while (rc==SQLITE_LOCKED || rc==SQLITE_BUSY);\
} while (0)


ProxySQL_Statistics::ProxySQL_Statistics() {
	statsdb_mem = new SQLite3DB();
	statsdb_mem->open((char *)"file:statsdb_mem?mode=memory&cache=shared", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX);
//	statsdb_disk = GloAdmin->statsdb_disk;
	statsdb_disk = new SQLite3DB();
//	char *dbname = (char *)malloc(strlen(GloVars.statsdb_disk)+50);
//	sprintf(dbname,"file:%s?cache=shared",GloVars.statsdb_disk);
	statsdb_disk->open((char *)GloVars.statsdb_disk, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX );
//	statsdb_disk->open((char *)GloVars.statsdb_disk, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOMUTEX);
//	statsdb_disk->open(dbname, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOMUTEX);
//	free(dbname);
	statsdb_disk->execute("PRAGMA synchronous=0");

	next_timer_MySQL_Threads_Handler = 0;
	next_timer_system_cpu = 0;
	next_timer_system_memory = 0;
}

ProxySQL_Statistics::~ProxySQL_Statistics() {
	drop_tables_defs(tables_defs_statsdb_mem);
	delete tables_defs_statsdb_mem;
	drop_tables_defs(tables_defs_statsdb_disk);
	delete tables_defs_statsdb_disk;
	delete statsdb_mem;
//	delete statsdb_disk;
}

void ProxySQL_Statistics::init() {

	tables_defs_statsdb_mem = new std::vector<table_def_t *>;
	tables_defs_statsdb_disk = new std::vector<table_def_t *>;
	insert_into_tables_defs(tables_defs_statsdb_mem,"mysql_connections", STATSDB_SQLITE_TABLE_MYSQL_CONNECTIONS);
	insert_into_tables_defs(tables_defs_statsdb_disk,"mysql_connections", STATSDB_SQLITE_TABLE_MYSQL_CONNECTIONS);
	insert_into_tables_defs(tables_defs_statsdb_disk,"system_cpu", STATSDB_SQLITE_TABLE_SYSTEM_CPU);
	insert_into_tables_defs(tables_defs_statsdb_disk,"system_memory", STATSDB_SQLITE_TABLE_SYSTEM_MEMORY);
	insert_into_tables_defs(tables_defs_statsdb_disk,"mysql_connections_hour", STATSDB_SQLITE_TABLE_MYSQL_CONNECTIONS_HOUR);
	insert_into_tables_defs(tables_defs_statsdb_disk,"system_cpu_hour", STATSDB_SQLITE_TABLE_SYSTEM_CPU_HOUR);
	insert_into_tables_defs(tables_defs_statsdb_disk,"system_memory_hour", STATSDB_SQLITE_TABLE_SYSTEM_MEMORY_HOUR);
	insert_into_tables_defs(tables_defs_statsdb_disk,"mysql_connections_day", STATSDB_SQLITE_TABLE_MYSQL_CONNECTIONS_DAY);
	insert_into_tables_defs(tables_defs_statsdb_disk,"system_cpu_day", STATSDB_SQLITE_TABLE_SYSTEM_CPU_DAY);
	insert_into_tables_defs(tables_defs_statsdb_disk,"system_memory_day", STATSDB_SQLITE_TABLE_SYSTEM_MEMORY_DAY);

	check_and_build_standard_tables(statsdb_mem, tables_defs_statsdb_disk);
	check_and_build_standard_tables(statsdb_disk, tables_defs_statsdb_disk);
}

void ProxySQL_Statistics::print_version() {
  fprintf(stderr,"Standard ProxySQL Statistics rev. %s -- %s -- %s\n", PROXYSQL_STATISTICS_VERSION, __FILE__, __TIMESTAMP__);
}


void ProxySQL_Statistics::check_and_build_standard_tables(SQLite3DB *db, std::vector<table_def_t *> *tables_defs) {
	table_def_t *td;
	db->execute("PRAGMA foreign_keys = OFF");
	for (std::vector<table_def_t *>::iterator it=tables_defs->begin(); it!=tables_defs->end(); ++it) {
		td=*it;
		db->check_and_build_table(td->table_name, td->table_def);
	}
	db->execute("PRAGMA foreign_keys = ON");
}



void ProxySQL_Statistics::insert_into_tables_defs(std::vector<table_def_t *> *tables_defs, const char *table_name, const char *table_def) {
	table_def_t *td = new table_def_t;
	td->table_name=strdup(table_name);
	td->table_def=strdup(table_def);
	tables_defs->push_back(td);
}

void ProxySQL_Statistics::drop_tables_defs(std::vector<table_def_t *> *tables_defs) {
	table_def_t *td;
	while (!tables_defs->empty()) {
		td=tables_defs->back();
		free(td->table_name);
		td->table_name=NULL;
		free(td->table_def);
		td->table_def=NULL;
		tables_defs->pop_back();
		delete td;
	}
}


bool ProxySQL_Statistics::MySQL_Threads_Handler_timetoget(unsigned long long curtime) {
	unsigned int i = (unsigned int)variables.stats_mysql_connections;
	if (i) {
		if (
			( curtime > next_timer_MySQL_Threads_Handler )
			||
			( curtime + i*1000*1000 < next_timer_MySQL_Threads_Handler )
		) {
			next_timer_MySQL_Threads_Handler = curtime/1000/1000 + i;
			next_timer_MySQL_Threads_Handler = next_timer_MySQL_Threads_Handler * 1000 * 1000;
			return true;
		}
	}
	return false;
}

bool ProxySQL_Statistics::system_cpu_timetoget(unsigned long long curtime) {
	unsigned int i = (unsigned int)variables.stats_system_cpu;
	if (i) {
		if (
			( curtime > next_timer_system_cpu )
			||
			( curtime + i*1000*1000 < next_timer_system_cpu )
		) {
			next_timer_system_cpu = curtime/1000/1000 + i;
			next_timer_system_cpu = next_timer_system_cpu * 1000 * 1000;
			return true;
		}
	}
	return false;
}

bool ProxySQL_Statistics::system_memory_timetoget(unsigned long long curtime) {
	unsigned int i = (unsigned int)variables.stats_system_memory;
	if (i) {
		if (
			( curtime > next_timer_system_memory )
			||
			( curtime + i*1000*1000 < next_timer_system_memory )
		) {
			next_timer_system_memory = curtime/1000/1000 + i;
			next_timer_system_memory = next_timer_system_memory * 1000 * 1000;
			return true;
		}
	}
	return false;
}

SQLite3_result * ProxySQL_Statistics::get_mysql_metrics() {
	SQLite3_result *resultset = NULL;
	int cols;
	int affected_rows;
	char *error = NULL;
	char *query = (char *)"SELECT * FROM (SELECT SUBSTR(FROM_UNIXTIME(timestamp),0,20) ts, timestamp, Client_Connections_aborted, Client_Connections_connected, Client_Connections_created, Server_Connections_aborted, Server_Connections_connected, Server_Connections_created, ConnPool_get_conn_failure, ConnPool_get_conn_immediate, ConnPool_get_conn_success, Questions FROM mysql_connections ORDER BY timestamp DESC LIMIT 100) t ORDER BY ts";
	statsdb_disk->execute_statement(query, &error , &cols , &affected_rows , &resultset);
	if (error) {
		if (resultset) {
			delete resultset;
			resultset = NULL;
		}
		free(error);
	}
	return resultset;
}

SQLite3_result * ProxySQL_Statistics::get_system_memory_metrics() {
	SQLite3_result *resultset = NULL;
	int cols;
	int affected_rows;
	char *error = NULL;
	char *query = (char *)"SELECT * FROM (SELECT SUBSTR(FROM_UNIXTIME(timestamp),0,20) ts, timestamp, allocated, resident, active, mapped, metadata, retained FROM system_memory ORDER BY timestamp DESC LIMIT 100) t ORDER BY ts";
	statsdb_disk->execute_statement(query, &error , &cols , &affected_rows , &resultset);
	if (error) {
		if (resultset) {
			delete resultset;
			resultset = NULL;
		}
		free(error);
	}
	return resultset;
}

SQLite3_result * ProxySQL_Statistics::get_system_cpu_metrics() {
	SQLite3_result *resultset = NULL;
	int cols;
	int affected_rows;
	char *error = NULL;
	char *query = (char *)"SELECT * FROM (SELECT SUBSTR(FROM_UNIXTIME(timestamp),0,20) ts, timestamp, tms_utime, tms_stime FROM system_cpu ORDER BY timestamp DESC LIMIT 100) t ORDER BY ts";
	statsdb_disk->execute_statement(query, &error , &cols , &affected_rows , &resultset);
	if (error) {
		if (resultset) {
			delete resultset;
			resultset = NULL;
		}
		free(error);
	}
	return resultset;
}

void ProxySQL_Statistics::system_cpu_sets() {
	int rc;
	struct tms buf;
	if (times(&buf) > -1) {
		sqlite3 *mydb3=statsdb_disk->get_db();
		sqlite3_stmt *statement1=NULL;
		char *query1=NULL;
		query1=(char *)"INSERT INTO system_cpu VALUES (?1, ?2, ?3)";
		rc=sqlite3_prepare_v2(mydb3, query1, -1, &statement1, 0);

		time_t ts = time(NULL);

		rc = sqlite3_bind_int64(statement1, 1, ts); assert(rc==SQLITE_OK);
		rc = sqlite3_bind_int64(statement1, 2, buf.tms_utime); assert(rc==SQLITE_OK);
		rc = sqlite3_bind_int64(statement1, 3, buf.tms_stime); assert(rc==SQLITE_OK);

		assert(rc==SQLITE_OK);
		SAFE_SQLITE3_STEP2(statement1);
		rc=sqlite3_clear_bindings(statement1); assert(rc==SQLITE_OK);
		rc=sqlite3_reset(statement1);
		sqlite3_finalize(statement1);
		
		SQLite3_result *resultset = NULL;
		int cols;
		int affected_rows;
		char *error = NULL;
		char *query = (char *)"SELECT MAX(timestamp) FROM system_cpu";
		statsdb_disk->execute_statement(query, &error , &cols , &affected_rows , &resultset);
		if (error) {
			if (resultset) {
				delete resultset;
				resultset = NULL;
			}
			free(error);
		} else {
			char buf[256];
			if (resultset->rows_count == 0) {
				sprintf(buf,"INSERT INTO system_cpu_hour SELECT timestamp/3600 , SUM(tms_utime), SUM(tms_stime) FROM system_cpu WHERE timestamp < %ld GROUP BY timestamp/3600", (ts/3600)*3600);
				statsdb_disk->execute(buf);
			} else {
				SQLite3_row *r = resultset->rows[0];
				time_t t = atol(r->fields[0]);
				if (ts >= t + 3600) {
					sprintf(buf,"INSERT INTO system_cpu_hour SELECT timestamp/3600 , SUM(tms_utime), SUM(tms_stime) FROM system_cpu WHERE timestamp >= %ld AND timestamp < %ld GROUP BY timestamp/3600", t+3600 , (ts/3600)*3600);
					statsdb_disk->execute(buf);
				}	
			}
			delete resultset;
			resultset = NULL;
			sprintf(buf,"DELETE FROM system_cpu WHERE timestamp < %ld", ts - 3600*24*7);
			statsdb_disk->execute(buf);
			sprintf(buf,"DELETE FROM system_cpu_hour WHERE timestamp < %ld", ts - 3600*24*365);
			statsdb_disk->execute(buf);
		}
	}
}

void ProxySQL_Statistics::system_memory_sets() {
	int rc;
	struct tms buf;
	if (times(&buf) > -1) {
		sqlite3 *mydb3=statsdb_disk->get_db();
		sqlite3_stmt *statement1=NULL;
		char *query1=NULL;
		query1=(char *)"INSERT INTO system_memory VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)";
		rc=sqlite3_prepare_v2(mydb3, query1, -1, &statement1, 0);

		time_t ts = time(NULL);

		size_t allocated = 0, resident = 0, active = 0, mapped = 0 , metadata = 0, retained = 0 , sz = sizeof(size_t);
		mallctl("stats.resident", &resident, &sz, NULL, 0);
		mallctl("stats.active", &active, &sz, NULL, 0);
		mallctl("stats.allocated", &allocated, &sz, NULL, 0);
		mallctl("stats.mapped", &mapped, &sz, NULL, 0);
		mallctl("stats.metadata", &metadata, &sz, NULL, 0);
		mallctl("stats.retained", &retained, &sz, NULL, 0);


		rc = sqlite3_bind_int64(statement1, 1, ts); assert(rc==SQLITE_OK);
		rc = sqlite3_bind_int64(statement1, 2, allocated); assert(rc==SQLITE_OK);
		rc = sqlite3_bind_int64(statement1, 3, resident); assert(rc==SQLITE_OK);
		rc = sqlite3_bind_int64(statement1, 4, active); assert(rc==SQLITE_OK);
		rc = sqlite3_bind_int64(statement1, 5, mapped); assert(rc==SQLITE_OK);
		rc = sqlite3_bind_int64(statement1, 6, metadata); assert(rc==SQLITE_OK);
		rc = sqlite3_bind_int64(statement1, 7, retained); assert(rc==SQLITE_OK);

		assert(rc==SQLITE_OK);
		SAFE_SQLITE3_STEP2(statement1);
		rc=sqlite3_clear_bindings(statement1); assert(rc==SQLITE_OK);
		rc=sqlite3_reset(statement1);
		sqlite3_finalize(statement1);

		SQLite3_result *resultset = NULL;
		int cols;
		int affected_rows;
		char *error = NULL;
		char *query = (char *)"SELECT MAX(timestamp) FROM system_memory";
		statsdb_disk->execute_statement(query, &error , &cols , &affected_rows , &resultset);
		if (error) {
			if (resultset) {
				delete resultset;
				resultset = NULL;
			}
			free(error);
		} else {
			char buf[256];
			if (resultset->rows_count == 0) {
				sprintf(buf,"INSERT INTO system_memory_hour SELECT timestamp/3600 , AVG(allocated), AVG(resident), AVG(active), AVG(mapped), AVG(metadata), AVG(retained) FROM system_memory WHERE timestamp < %ld GROUP BY timestamp/3600", (ts/3600)*3600);
				statsdb_disk->execute(buf);
			} else {
				SQLite3_row *r = resultset->rows[0];
				time_t t = atol(r->fields[0]);
				if (ts >= t + 3600) {
					sprintf(buf,"INSERT INTO system_memory_hour SELECT timestamp/3600 , AVG(allocated), AVG(resident), AVG(active), AVG(mapped), AVG(metadata), AVG(retained) FROM system_memory WHERE timestamp >= %ld AND timestamp < %ld GROUP BY timestamp/3600", t+3600 , (ts/3600)*3600);
					statsdb_disk->execute(buf);
				}	
			}
			delete resultset;
			resultset = NULL;
			sprintf(buf,"DELETE FROM system_memory WHERE timestamp < %ld", ts - 3600*24*7);
			statsdb_disk->execute(buf);
			sprintf(buf,"DELETE FROM system_memory_hour WHERE timestamp < %ld", ts - 3600*24*365);
			statsdb_disk->execute(buf);
		}
	}
}

void ProxySQL_Statistics::MySQL_Threads_Handler_sets(SQLite3_result *resultset) {
	int rc;
	if (resultset == NULL)
		return;
	sqlite3 *mydb3=statsdb_disk->get_db();
	sqlite3_stmt *statement1=NULL;
	//sqlite3_stmt *statement2=NULL;
	//sqlite3_stmt *statement3=NULL;
	char *query1=NULL;
	//char *query2=NULL;
	//char *query3=NULL;
	query1=(char *)"INSERT INTO mysql_connections VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)";
	rc=sqlite3_prepare_v2(mydb3, query1, -1, &statement1, 0);
	assert(rc==SQLITE_OK);
	//rc=sqlite3_prepare_v2(mydb3, query2, -1, &statement2, 0);
	//assert(rc==SQLITE_OK);
	//rc=sqlite3_prepare_v2(mydb3, query3, -1, &statement3, 0);
	//assert(rc==SQLITE_OK);


	int mysql_connections_values[12];
	for (int i=0; i<12; i++) {
		mysql_connections_values[i]=0;
	}
	mysql_connections_values[0] = time(NULL);

	for (std::vector<SQLite3_row *>::iterator it = resultset->rows.begin() ; it != resultset->rows.end(); ++it) {
		SQLite3_row *r1=*it;
		if (!strcasecmp(r1->fields[0],"Client_Connections_aborted")) {
			mysql_connections_values[1]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"Client_Connections_connected")) {
			mysql_connections_values[2]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"Client_Connections_created")) {
			mysql_connections_values[3]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"Server_Connections_aborted")) {
			mysql_connections_values[4]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"Server_Connections_connected")) {
			mysql_connections_values[5]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"Server_Connections_created")) {
			mysql_connections_values[6]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"ConnPool_get_conn_failure")) {
			mysql_connections_values[7]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"ConnPool_get_conn_immediate")) {
			mysql_connections_values[8]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"ConnPool_get_conn_success")) {
			mysql_connections_values[9]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"Questions")) {
			mysql_connections_values[10]=atoi(r1->fields[1]);
			continue;
		}
		if (!strcasecmp(r1->fields[0],"Slow_queries")) {
			mysql_connections_values[11]=atoi(r1->fields[1]);
			continue;
		}
	}

	for (int i=0; i<12; i++) {
		rc=sqlite3_bind_int64(statement1, i+1, mysql_connections_values[i]); assert(rc==SQLITE_OK);
	}

	SAFE_SQLITE3_STEP2(statement1);
	rc=sqlite3_clear_bindings(statement1); assert(rc==SQLITE_OK);
	rc=sqlite3_reset(statement1); //assert(rc==SQLITE_OK);
	sqlite3_finalize(statement1);
}
