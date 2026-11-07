#include "web_server.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "recorder_state.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server;

static bool valid_name(const char *name)
{
    if (!name || strlen(name) != 11) return false;
    if (strncmp(name, "REC", 3) != 0 || strcmp(name + 7, ".WAV") != 0) return false;
    for (int i = 3; i < 7; ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
    }
    return true;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,"
        "initial-scale=1'><title>REC DIAL</title><style>"
        "body{font-family:Arial;background:#101820;color:#eee;padding:20px}"
        ".item{background:#20303b;padding:14px;margin:12px 0;border-radius:10px}"
        "a,button{background:#159b83;color:white;padding:10px;border:0;border-radius:7px;"
        "text-decoration:none;display:inline-block;margin:5px 5px 5px 0}"
        ".del{background:#b83b45}small{color:#bac5cc}</style></head><body>"
        "<h1>REC DIAL</h1><p>Saved recordings</p>");

    DIR *dir = opendir(FLASH_MOUNT_POINT);
    if (!dir) {
        httpd_resp_sendstr_chunk(req, "<p>Storage folder could not be opened.</p>");
    } else {
        struct dirent *e;
        char row[512], path[64];
        while ((e = readdir(dir)) != NULL) {
            if (!valid_name(e->d_name)) continue;
            snprintf(path, sizeof(path), "%s/%.11s", FLASH_MOUNT_POINT, e->d_name);
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            snprintf(row, sizeof(row),
                "<div class='item'><b>%.11s</b><br><small>%lu bytes</small><br>"
                "<audio controls preload='none' src='/download?file=%.11s'></audio><br>"
                "<a href='/download?file=%.11s'>Download WAV</a>"
                "<a class='del' href='/delete?file=%.11s' "
                "onclick=\"return confirm('Delete this recording?')\">Delete</a></div>",
                e->d_name, (unsigned long)st.st_size,
                e->d_name, e->d_name, e->d_name);
            httpd_resp_sendstr_chunk(req, row);
        }
        closedir(dir);
    }
    httpd_resp_sendstr_chunk(req, "</body></html>");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t download_handler(httpd_req_t *req)
{
    char query[96], name[20], path[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", name, sizeof(name)) != ESP_OK ||
        !valid_name(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    snprintf(path, sizeof(path), "%s/%s", FLASH_MOUNT_POINT, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Recording not found");
        return ESP_FAIL;
    }

    char header[64];
    snprintf(header, sizeof(header), "attachment; filename=\"%s\"", name);
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Content-Disposition", header);

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t delete_handler(httpd_req_t *req)
{
    char query[96], name[20], path[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", name, sizeof(name)) != ESP_OK ||
        !valid_name(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    snprintf(path, sizeof(path), "%s/%s", FLASH_MOUNT_POINT, name);
    if (unlink(path) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Delete failed");
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    ESP_LOGI(TAG, "Deleted %s", name);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 6144;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t index_uri = {
        .uri = "/", .method = HTTP_GET, .handler = index_handler
    };
    const httpd_uri_t download_uri = {
        .uri = "/download", .method = HTTP_GET, .handler = download_handler
    };
    const httpd_uri_t delete_uri = {
        .uri = "/delete", .method = HTTP_GET, .handler = delete_handler
    };

    if ((err = httpd_register_uri_handler(s_server, &index_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &download_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &delete_uri)) != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Web page ready: http://192.168.4.1/");
    return ESP_OK;
}
