#include "db.h"

static sqlite3 *g_db = NULL;

int db_init(const char *db_path)
{

    int rc = sqlite3_open(db_path, &g_db);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    const char *sql_create_table = "CREATE TABLE IF NOT EXISTS sensor_logs ("
                                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                   "accel_peak REAL, accel_rms REAL,"
                                   "gyro_x REAL, gyro_y REAL, gyro_z REAL,"
                                   "timestamp INTEGER,"
                                   "is_sent INTEGER DEFAULT 0);";

    char *err = NULL;
    sqlite3_exec(g_db, sql_create_table, NULL, NULL, &err);
    if (err)
    {
        sqlite3_free(err);
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    // WAL模式 分块合并，不直接操作主文件
    sqlite3_exec(g_db, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);

    edge_log(LOG_INFO, "Database initialized at %s", db_path);
    return 0;
}

int db_deinit(void)
{
    if (g_db)
    {
        sqlite3_close(g_db);
        g_db = NULL;
    }
    return 0;
}

int db_save_sensor_record(const sensor_data_t *data)
{
    if (!g_db)
        return -1;

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_db,
                                "INSERT INTO sensor_logs (accel_peak, accel_rms, gyro_x, gyro_y, gyro_z, timestamp, is_sent) VALUES (?, ?, ?, ?, ?, ?, ?);",
                                -1, &stmt, NULL);

    if (rc != SQLITE_OK)
    {
        edge_log(LOG_ERROR, "Failed to prepare SQL statement: %s", sqlite3_errmsg(g_db));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_bind_double(stmt, 1, (double)data->accel_peak);
    sqlite3_bind_double(stmt, 2, (double)data->accel_rms);
    sqlite3_bind_double(stmt, 3, (double)data->gyro_mean_x);
    sqlite3_bind_double(stmt, 4, (double)data->gyro_mean_y);
    sqlite3_bind_double(stmt, 5, (double)data->gyro_mean_z);
    sqlite3_bind_int(stmt, 6, data->timestamp);
    sqlite3_bind_int(stmt, 7, 0); // 存入时默认为未发送

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE)
    {
        edge_log(LOG_ERROR, "Failed to insert record into database: %s", sqlite3_errmsg(g_db));
    }

    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE ? 0 : -1;
}

int db_get_unsent_records(sensor_data_t *out_list, int max_count)
{
    if (!g_db)
        return -1;
    sqlite3_stmt *stmt;
    int count = 0;
    const char *sql = "SELECT id, accel_peak, accel_rms, gyro_x, gyro_y, gyro_z, timestamp FROM sensor_logs "
                      "WHERE is_sent = 0 ORDER BY timestamp ASC LIMIT ?;";

    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, max_count);

    // 重点：循环读取每一行
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count)
    {
        out_list[count].db_id = sqlite3_column_int(stmt, 0);
        out_list[count].accel_peak = (float)sqlite3_column_double(stmt, 1);
        out_list[count].accel_rms = (float)sqlite3_column_double(stmt, 2);
        out_list[count].gyro_mean_x = (float)sqlite3_column_double(stmt, 3);
        out_list[count].gyro_mean_y = (float)sqlite3_column_double(stmt, 4);
        out_list[count].gyro_mean_z = (float)sqlite3_column_double(stmt, 5);
        out_list[count].timestamp = sqlite3_column_int64(stmt, 6);
        count++;
    }

    sqlite3_finalize(stmt);
    return count; // 返回实际查到了多少条
}

int db_mark_as_sent(int id)
{
    if (!g_db)
        return -1;
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE sensor_logs SET is_sent = 1 WHERE id = ?;";

    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, id);

    int rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE)
    {
        // 打印错误日志
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int db_cleanup_old_data(int days)
{
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM sensor_logs WHERE timestamp < strftime('%%s', 'now', '-%d days');", days);

    char *err_msg = NULL;
    if (sqlite3_exec(g_db, sql, NULL, NULL, &err_msg) != SQLITE_OK)
    {
        edge_log(LOG_ERROR, "SQL error during cleanup: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    const char *sql_create_index = "CREATE INDEX IF NOT EXISTS idx_timestamp ON sensor_logs(timestamp);";
    sqlite3_exec(g_db, sql_create_index, NULL, NULL, &err_msg);
    if (err_msg)
    {
        // 打印日志，但这里不认为是致命错误
        edge_log(LOG_ERROR, "Failed to create index: %s", err_msg);
        sqlite3_free(err_msg);
    }

    return 0;
}