#ifndef __PROXYSQL_RESTAPI_H__
#define __PROXYSQL_RESTAPI_H__

#include "proxy_defines.h"
#include "proxysql.h"
#include "cpp.h"
#include <vector>

class SQLite3DB;

class Restapi_Row {
public:
	unsigned int id;
	bool is_active;
	unsigned int timeout_ms;
	std::string method;
	std::string uri;
	std::string script;
	std::string comment;
	unsigned int version;
	Restapi_Row(unsigned int _id, bool _is_active, unsigned int _in, const std::string& _method, const std::string& _uri, const std::string& _script, const std::string& _comment);
};

class ProxySQL_Restapi {
public:
	SQLite3DB* admindb;
	ProxySQL_Restapi(SQLite3DB* db);
	virtual ~ProxySQL_Restapi();

	unsigned int last_version;
	unsigned int version;
#ifdef PA_PTHREAD_MUTEX
	pthread_rwlock_t rwlock;
#else
	rwlock_t rwlock;
#endif
	std::vector<Restapi_Row> Restapi_Rows;
	void update_restapi_table(SQLite3_result *result);
	void load_restapi_to_runtime();
	void save_restapi_runtime_to_database(bool);
};

#endif // #ifndef __PROXYSQL_RESTAPI_H__
