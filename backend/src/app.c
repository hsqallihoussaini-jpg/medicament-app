#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef SOCKET socket_t;
#define close_socket closesocket
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define close_socket close
#endif

#include "app.h"

#define REQUEST_BUFFER_SIZE 65536
#define RESPONSE_BUFFER_INITIAL_SIZE 1024
#define SUPABASE_MEDICAMENTS_PATH "/rest/v1/medicaments?select=*&order=created_at.desc"
#define SUPABASE_INSERT_PATH "/rest/v1/medicaments"
#define SUPABASE_PRISES_PATH "/rest/v1/prises?select=*&order=taken_at.desc"
#define SUPABASE_PARTAGES_PATH "/rest/v1/rappels_partages"
#define SUPABASE_RENDEZVOUS_PATH "/rest/v1/rendez_vous?select=*&order=date_heure.asc"
#define GEMINI_MODEL_PATH "/v1beta/models/gemini-1.5-flash:generateContent"

typedef struct {
    char *data;
    size_t size;
} MemoryBuffer;

static const char *get_env_or_empty(const char *name) {
    const char *value = getenv(name);
    return value != NULL ? value : "";
}

static void trim_trailing_slashes(const char *input, char *output, size_t output_size) {
    size_t length = strlen(input);
    while (length > 0 && input[length - 1] == '/') {
        length--;
    }

    if (length >= output_size) {
        length = output_size - 1;
    }

    memcpy(output, input, length);
    output[length] = '\0';
}

static char *duplicate_string(const char *value) {
    size_t length = strlen(value);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static void memory_buffer_init(MemoryBuffer *buffer) {
    buffer->data = (char *)malloc(RESPONSE_BUFFER_INITIAL_SIZE);
    buffer->size = 0;
    if (buffer->data != NULL) {
        buffer->data[0] = '\0';
    }
}

static void memory_buffer_free(MemoryBuffer *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}

static size_t http_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryBuffer *buffer = (MemoryBuffer *)userp;
    char *ptr = (char *)realloc(buffer->data, buffer->size + realsize + 1);

    if (ptr == NULL) {
        return 0;
    }

    buffer->data = ptr;
    memcpy(&(buffer->data[buffer->size]), contents, realsize);
    buffer->size += realsize;
    buffer->data[buffer->size] = '\0';
    return realsize;
}

static char *perform_curl_request(const char *url, const char *method, struct curl_slist *headers, const char *body, long *status_code) {
    CURL *curl = curl_easy_init();
    MemoryBuffer response;
    CURLcode result;

    if (curl == NULL) {
        return NULL;
    }

    memory_buffer_init(&response);
    if (response.data == NULL) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    if (headers != NULL) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    if (method != NULL && strcmp(method, "GET") != 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    }

    if (body != NULL) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }

    result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        memory_buffer_free(&response);
        curl_easy_cleanup(curl);
        return NULL;
    }

    if (status_code != NULL) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status_code);
    }

    curl_easy_cleanup(curl);
    return response.data;
}

static struct curl_slist *build_supabase_headers(const char *api_key, int include_prefer) {
    char auth_header[1024];
    char api_header[1024];
    struct curl_slist *headers = NULL;

    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    snprintf(api_header, sizeof(api_header), "apikey: %s", api_key);

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, api_header);
    headers = curl_slist_append(headers, auth_header);

    if (include_prefer) {
        headers = curl_slist_append(headers, "Prefer: return=representation");
    }

    return headers;
}

static char *make_error_json(const char *message) {
    cJSON *root = cJSON_CreateObject();
    char *printed = NULL;

    if (root == NULL) {
        return duplicate_string("{\"error\":\"unknown error\"}");
    }

    cJSON_AddStringToObject(root, "error", message);
    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (printed == NULL) {
        return duplicate_string("{\"error\":\"unknown error\"}");
    }

    return printed;
}

static char *make_message_json(const char *key, const char *value) {
    cJSON *root = cJSON_CreateObject();
    char *printed = NULL;

    if (root == NULL) {
        return make_error_json("memory allocation failed");
    }

    cJSON_AddStringToObject(root, key, value);
    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return printed;
}

static char *extract_text_from_gemini_response(const char *response_body) {
    cJSON *root = cJSON_Parse(response_body);
    cJSON *candidates = NULL;
    cJSON *candidate = NULL;
    cJSON *content = NULL;
    cJSON *parts = NULL;
    cJSON *part = NULL;
    cJSON *error = NULL;
    cJSON *message = NULL;
    char *result = NULL;

    if (root == NULL) {
        return duplicate_string("Réponse Gemini invalide.");
    }

    error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (cJSON_IsObject(error)) {
        message = cJSON_GetObjectItemCaseSensitive(error, "message");
        if (cJSON_IsString(message) && message->valuestring != NULL) {
            result = duplicate_string(message->valuestring);
        }
        cJSON_Delete(root);
        if (result != NULL) {
            return result;
        }
        return duplicate_string("Erreur Gemini inconnue.");
    }

    candidates = cJSON_GetObjectItemCaseSensitive(root, "candidates");
    if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        candidate = cJSON_GetArrayItem(candidates, 0);
        if (cJSON_IsObject(candidate)) {
            content = cJSON_GetObjectItemCaseSensitive(candidate, "content");
            if (cJSON_IsObject(content)) {
                parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
                if (cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
                    part = cJSON_GetArrayItem(parts, 0);
                    if (cJSON_IsObject(part)) {
                        message = cJSON_GetObjectItemCaseSensitive(part, "text");
                        if (cJSON_IsString(message) && message->valuestring != NULL) {
                            result = duplicate_string(message->valuestring);
                        }
                    }
                }
            }
        }
    }

    if (result == NULL) {
        result = duplicate_string("La réponse Gemini ne contient pas de texte exploitable.");
    }

    cJSON_Delete(root);
    return result;
}

static const char *find_header_value(const char *request, const char *header_name) {
    const char *line = strstr(request, "\r\n");
    size_t header_name_length = strlen(header_name);

    if (line == NULL) {
        return NULL;
    }

    line += 2;
    while (*line != '\0') {
        const char *next = strstr(line, "\r\n");
        const char *colon = NULL;
        size_t line_length;

        if (next == NULL) {
            break;
        }

        if (next == line) {
            break;
        }

        line_length = (size_t)(next - line);
        colon = memchr(line, ':', line_length);
        if (colon != NULL) {
            size_t key_length = (size_t)(colon - line);
            if (key_length == header_name_length && strncasecmp(line, header_name, header_name_length) == 0) {
                const char *value = colon + 1;
                while (value < next && (*value == ' ' || *value == '\t')) {
                    value++;
                }
                return value;
            }
        }

        line = next + 2;
    }

    return NULL;
}

static size_t get_content_length(const char *request) {
    const char *value = find_header_value(request, "Content-Length");
    if (value == NULL) {
        return 0;
    }

    return (size_t)strtoul(value, NULL, 10);
}

static char *find_request_body(char *request) {
    char *body = strstr(request, "\r\n\r\n");
    if (body == NULL) {
        return NULL;
    }

    return body + 4;
}

static void split_path_and_query(const char *raw_path, char *path_only, size_t path_size, const char **query_out) {
    const char *query = strchr(raw_path, '?');
    if (query == NULL) {
        snprintf(path_only, path_size, "%s", raw_path);
        *query_out = NULL;
        return;
    }

    size_t path_length = (size_t)(query - raw_path);
    if (path_length >= path_size) {
        path_length = path_size - 1;
    }

    memcpy(path_only, raw_path, path_length);
    path_only[path_length] = '\0';
    *query_out = query + 1;
}

static int query_get_value(const char *query, const char *key, char *out_value, size_t out_size) {
    const char *cursor = query;
    size_t key_length = strlen(key);

    if (query == NULL || key == NULL || out_value == NULL || out_size == 0) {
        return 0;
    }

    while (*cursor != '\0') {
        const char *pair_end = strchr(cursor, '&');
        const char *equals = strchr(cursor, '=');
        size_t pair_length;

        if (pair_end == NULL) {
            pair_end = cursor + strlen(cursor);
        }

        pair_length = (size_t)(pair_end - cursor);
        if (equals != NULL && equals < pair_end) {
            size_t current_key_length = (size_t)(equals - cursor);
            if (current_key_length == key_length && strncmp(cursor, key, key_length) == 0) {
                size_t value_length = (size_t)(pair_end - equals - 1);
                if (value_length >= out_size) {
                    value_length = out_size - 1;
                }
                memcpy(out_value, equals + 1, value_length);
                out_value[value_length] = '\0';
                return 1;
            }
        }

        if (*pair_end == '\0') {
            break;
        }
        cursor = pair_end + 1;
    }

    return 0;
}

static int json_number_or_default(cJSON *value, int default_value) {
    if (!cJSON_IsNumber(value)) {
        return default_value;
    }
    return value->valueint;
}

static int send_all(socket_t client, const char *data, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        int bytes_sent = send(client, data + sent, (int)(length - sent), 0);
        if (bytes_sent <= 0) {
            return 0;
        }
        sent += (size_t)bytes_sent;
    }

    return 1;
}

static void send_json_response(socket_t client, int status_code, const char *status_text, const char *body) {
    char header[1024];
    size_t body_length = strlen(body);
    int header_length = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Access-Control-Allow-Methods: GET,POST,PATCH,DELETE,OPTIONS\r\n"
        "Connection: close\r\n\r\n",
        status_code,
        status_text,
        body_length);

    if (header_length > 0) {
        send_all(client, header, (size_t)header_length);
    }
    send_all(client, body, body_length);
}

static void send_no_content_response(socket_t client) {
    const char *header =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Access-Control-Allow-Methods: GET,POST,PATCH,DELETE,OPTIONS\r\n"
        "Connection: close\r\n"
        "Content-Length: 0\r\n\r\n";
    send_all(client, header, strlen(header));
}

static void send_text_error(socket_t client, int status_code, const char *status_text, const char *message) {
    char *body = make_error_json(message);
    if (body == NULL) {
        body = duplicate_string("{\"error\":\"internal server error\"}");
    }
    send_json_response(client, status_code, status_text, body);
    free(body);
}

static char *read_full_request(socket_t client) {
    char *buffer = (char *)malloc(REQUEST_BUFFER_SIZE + 1);
    size_t total = 0;

    if (buffer == NULL) {
        return NULL;
    }

    while (total < REQUEST_BUFFER_SIZE) {
        int received = recv(client, buffer + total, (int)(REQUEST_BUFFER_SIZE - total), 0);
        if (received <= 0) {
            break;
        }
        total += (size_t)received;
        buffer[total] = '\0';

        if (strstr(buffer, "\r\n\r\n") != NULL) {
            size_t content_length = get_content_length(buffer);
            char *body = find_request_body(buffer);

            if (body == NULL) {
                break;
            }

            size_t header_size = (size_t)(body - buffer);

            if (total >= header_size + content_length) {
                return buffer;
            }
        }
    }

    if (total > 0) {
        char *headers_end = strstr(buffer, "\r\n\r\n");
        if (headers_end != NULL) {
            size_t content_length = get_content_length(buffer);
            size_t header_size = (size_t)(headers_end - buffer + 4);
            if (total >= header_size + content_length) {
                return buffer;
            }
        }
    }

    free(buffer);
    return NULL;
}

static char *supabase_get_medicaments(void) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    char *response = NULL;
    long status_code = 0;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    snprintf(url, sizeof(url), "%s%s", supabase_url, SUPABASE_MEDICAMENTS_PATH);
    headers = build_supabase_headers(api_key_copy, 0);
    response = perform_curl_request(url, "GET", headers, NULL, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);

    if (response == NULL) {
        return make_error_json("Impossible de récupérer les médicaments depuis Supabase.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    return response;
}

static char *supabase_insert_medicament(
    const char *nom,
    const char *dose,
    const char *heure,
    const char *photo_url,
    int quantite_restante,
    int seuil_alerte,
    const char *proche_nom,
    const char *proche_contact) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    cJSON *payload = NULL;
    char *payload_json = NULL;
    char *response = NULL;
    long status_code = 0;
    cJSON *parsed = NULL;
    char *formatted = NULL;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    payload = cJSON_CreateObject();
    if (payload == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de préparer le corps JSON.");
    }

    cJSON_AddStringToObject(payload, "nom", nom);
    cJSON_AddStringToObject(payload, "dose", dose);
    cJSON_AddStringToObject(payload, "heure", heure);
    cJSON_AddNumberToObject(payload, "quantite_restante", quantite_restante < 0 ? 0 : quantite_restante);
    cJSON_AddNumberToObject(payload, "seuil_alerte", seuil_alerte < 0 ? 0 : seuil_alerte);

    if (photo_url != NULL && photo_url[0] != '\0') {
        cJSON_AddStringToObject(payload, "photo_url", photo_url);
    }
    if (proche_nom != NULL && proche_nom[0] != '\0') {
        cJSON_AddStringToObject(payload, "proche_nom", proche_nom);
    }
    if (proche_contact != NULL && proche_contact[0] != '\0') {
        cJSON_AddStringToObject(payload, "proche_contact", proche_contact);
    }
    payload_json = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (payload_json == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de sérialiser le médicament.");
    }

    snprintf(url, sizeof(url), "%s%s", supabase_url, SUPABASE_INSERT_PATH);
    headers = build_supabase_headers(api_key_copy, 1);
    response = perform_curl_request(url, "POST", headers, payload_json, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);
    free(payload_json);

    if (response == NULL) {
        return make_error_json("Impossible d'ajouter le médicament dans Supabase.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    parsed = cJSON_Parse(response);
    if (parsed == NULL) {
        return response;
    }

    if (cJSON_IsArray(parsed) && cJSON_GetArraySize(parsed) > 0) {
        cJSON *first = cJSON_GetArrayItem(parsed, 0);
        formatted = cJSON_PrintUnformatted(first);
        cJSON_Delete(parsed);
        free(response);
        if (formatted != NULL) {
            return formatted;
        }
        return make_error_json("Impossible de formater la réponse Supabase.");
    }

    cJSON_Delete(parsed);
    return response;
}

static char *supabase_delete_medicament(const char *medicament_id) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    char *response = NULL;
    long status_code = 0;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    snprintf(url, sizeof(url), "%s/rest/v1/medicaments?id=eq.%s", supabase_url, medicament_id);
    headers = build_supabase_headers(api_key_copy, 0);
    response = perform_curl_request(url, "DELETE", headers, NULL, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);

    if (response == NULL) {
        return make_message_json("message", "Médicament supprimé.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    free(response);
    return make_message_json("message", "Médicament supprimé.");
}

static char *supabase_get_prises(const char *medicament_id_filter) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    char *response = NULL;
    long status_code = 0;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    if (medicament_id_filter != NULL && medicament_id_filter[0] != '\0') {
        snprintf(url, sizeof(url), "%s/rest/v1/prises?select=*&medicament_id=eq.%s&order=taken_at.desc", supabase_url, medicament_id_filter);
    } else {
        snprintf(url, sizeof(url), "%s%s", supabase_url, SUPABASE_PRISES_PATH);
    }

    headers = build_supabase_headers(api_key_copy, 0);
    response = perform_curl_request(url, "GET", headers, NULL, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);

    if (response == NULL) {
        return make_error_json("Impossible de récupérer l'historique des prises.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    return response;
}

static char *supabase_insert_prise(const char *medicament_id, const char *statut, const char *commentaire) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    cJSON *payload = NULL;
    char *payload_json = NULL;
    char *response = NULL;
    long status_code = 0;
    cJSON *parsed = NULL;
    char *formatted = NULL;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    payload = cJSON_CreateObject();
    if (payload == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de préparer la prise.");
    }

    cJSON_AddNumberToObject(payload, "medicament_id", atoi(medicament_id));
    cJSON_AddStringToObject(payload, "statut", statut != NULL && statut[0] != '\0' ? statut : "prise_confirmee");
    if (commentaire != NULL && commentaire[0] != '\0') {
        cJSON_AddStringToObject(payload, "commentaire", commentaire);
    }
    payload_json = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (payload_json == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de sérialiser la prise.");
    }

    snprintf(url, sizeof(url), "%s/rest/v1/prises", supabase_url);
    headers = build_supabase_headers(api_key_copy, 1);
    response = perform_curl_request(url, "POST", headers, payload_json, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);
    free(payload_json);

    if (response == NULL) {
        return make_error_json("Impossible d'enregistrer la prise.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    parsed = cJSON_Parse(response);
    if (parsed == NULL) {
        return response;
    }

    if (cJSON_IsArray(parsed) && cJSON_GetArraySize(parsed) > 0) {
        formatted = cJSON_PrintUnformatted(cJSON_GetArrayItem(parsed, 0));
        cJSON_Delete(parsed);
        free(response);
        return formatted != NULL ? formatted : make_error_json("Impossible de formater la prise.");
    }

    cJSON_Delete(parsed);
    return response;
}

static char *supabase_update_stock(const char *medicament_id, int quantite_restante) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    cJSON *payload = NULL;
    char *payload_json = NULL;
    char *response = NULL;
    long status_code = 0;
    cJSON *parsed = NULL;
    char *formatted = NULL;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    payload = cJSON_CreateObject();
    if (payload == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de préparer la mise à jour de stock.");
    }

    cJSON_AddNumberToObject(payload, "quantite_restante", quantite_restante < 0 ? 0 : quantite_restante);
    payload_json = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (payload_json == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de sérialiser la mise à jour de stock.");
    }

    snprintf(url, sizeof(url), "%s/rest/v1/medicaments?id=eq.%s", supabase_url, medicament_id);
    headers = build_supabase_headers(api_key_copy, 1);
    response = perform_curl_request(url, "PATCH", headers, payload_json, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);
    free(payload_json);

    if (response == NULL) {
        return make_error_json("Impossible de mettre à jour le stock.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    parsed = cJSON_Parse(response);
    if (parsed == NULL) {
        return response;
    }

    if (cJSON_IsArray(parsed) && cJSON_GetArraySize(parsed) > 0) {
        formatted = cJSON_PrintUnformatted(cJSON_GetArrayItem(parsed, 0));
        cJSON_Delete(parsed);
        free(response);
        return formatted != NULL ? formatted : make_error_json("Impossible de formater le stock mis à jour.");
    }

    cJSON_Delete(parsed);
    return response;
}

static char *supabase_insert_partage_rappel(const char *medicament_id, const char *destinataire, const char *message) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    cJSON *payload = NULL;
    char *payload_json = NULL;
    char *response = NULL;
    long status_code = 0;
    cJSON *parsed = NULL;
    char *formatted = NULL;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    payload = cJSON_CreateObject();
    if (payload == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de préparer le partage de rappel.");
    }

    cJSON_AddNumberToObject(payload, "medicament_id", atoi(medicament_id));
    cJSON_AddStringToObject(payload, "destinataire", destinataire);
    cJSON_AddStringToObject(payload, "message", message);
    payload_json = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (payload_json == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de sérialiser le partage de rappel.");
    }

    snprintf(url, sizeof(url), "%s%s", supabase_url, SUPABASE_PARTAGES_PATH);
    headers = build_supabase_headers(api_key_copy, 1);
    response = perform_curl_request(url, "POST", headers, payload_json, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);
    free(payload_json);

    if (response == NULL) {
        return make_error_json("Impossible d'enregistrer le partage de rappel.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    parsed = cJSON_Parse(response);
    if (parsed == NULL) {
        return response;
    }

    if (cJSON_IsArray(parsed) && cJSON_GetArraySize(parsed) > 0) {
        formatted = cJSON_PrintUnformatted(cJSON_GetArrayItem(parsed, 0));
        cJSON_Delete(parsed);
        free(response);
        return formatted != NULL ? formatted : make_error_json("Impossible de formater le partage de rappel.");
    }

    cJSON_Delete(parsed);
    return response;
}

static char *supabase_get_stats(void) {
    char *medicaments_raw = supabase_get_medicaments();
    char *prises_raw = supabase_get_prises(NULL);
    cJSON *medicaments = NULL;
    cJSON *prises = NULL;
    cJSON *stats = NULL;
    int total_medicaments = 0;
    int total_prises = 0;
    int low_stock_count = 0;
    int adherence_score = 0;
    char today[16];
    size_t today_len;
    char *printed = NULL;

    if (medicaments_raw == NULL || prises_raw == NULL) {
        free(medicaments_raw);
        free(prises_raw);
        return make_error_json("Impossible de récupérer les données pour les statistiques.");
    }

    medicaments = cJSON_Parse(medicaments_raw);
    prises = cJSON_Parse(prises_raw);
    free(medicaments_raw);
    free(prises_raw);

    if (!cJSON_IsArray(medicaments) || !cJSON_IsArray(prises)) {
        cJSON_Delete(medicaments);
        cJSON_Delete(prises);
        return make_error_json("Données statistiques invalides.");
    }

    total_medicaments = cJSON_GetArraySize(medicaments);
    total_prises = cJSON_GetArraySize(prises);

    {
        cJSON *med = NULL;
        cJSON_ArrayForEach(med, medicaments) {
            int stock = json_number_or_default(cJSON_GetObjectItemCaseSensitive(med, "quantite_restante"), 0);
            int seuil = json_number_or_default(cJSON_GetObjectItemCaseSensitive(med, "seuil_alerte"), 2);
            if (stock <= seuil) {
                low_stock_count++;
            }
        }
    }

    {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        if (tm_now != NULL) {
            strftime(today, sizeof(today), "%Y-%m-%d", tm_now);
        } else {
            snprintf(today, sizeof(today), "1970-01-01");
        }
    }
    today_len = strlen(today);

    {
        cJSON *prise = NULL;
        int taken_today = 0;
        cJSON_ArrayForEach(prise, prises) {
            cJSON *taken_at = cJSON_GetObjectItemCaseSensitive(prise, "taken_at");
            if (cJSON_IsString(taken_at) && taken_at->valuestring != NULL && strncmp(taken_at->valuestring, today, today_len) == 0) {
                taken_today++;
            }
        }

        if (total_medicaments > 0) {
            adherence_score = (int)((taken_today * 100.0) / total_medicaments);
            if (adherence_score > 100) {
                adherence_score = 100;
            }
        }
    }

    stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(stats, "total_medicaments", total_medicaments);
    cJSON_AddNumberToObject(stats, "total_prises", total_prises);
    cJSON_AddNumberToObject(stats, "low_stock_count", low_stock_count);
    cJSON_AddNumberToObject(stats, "adherence_score", adherence_score);

    printed = cJSON_PrintUnformatted(stats);
    cJSON_Delete(stats);
    cJSON_Delete(medicaments);
    cJSON_Delete(prises);

    if (printed == NULL) {
        return make_error_json("Impossible de formater les statistiques.");
    }
    return printed;
}

static char *supabase_get_rendezvous(void) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    char *response = NULL;
    long status_code = 0;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    snprintf(url, sizeof(url), "%s%s", supabase_url, SUPABASE_RENDEZVOUS_PATH);
    headers = build_supabase_headers(api_key_copy, 0);
    response = perform_curl_request(url, "GET", headers, NULL, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);

    if (response == NULL) {
        return make_error_json("Impossible de récupérer les rendez-vous médicaux.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    return response;
}

static char *supabase_insert_rendezvous(
    const char *titre,
    const char *medecin,
    const char *date_heure,
    const char *lieu,
    const char *notes,
    int rappel_minutes) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    cJSON *payload = NULL;
    char *payload_json = NULL;
    char *response = NULL;
    long status_code = 0;
    cJSON *parsed = NULL;
    char *formatted = NULL;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    payload = cJSON_CreateObject();
    if (payload == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de préparer le rendez-vous médical.");
    }

    cJSON_AddStringToObject(payload, "titre", titre);
    cJSON_AddStringToObject(payload, "medecin", medecin);
    cJSON_AddStringToObject(payload, "date_heure", date_heure);
    cJSON_AddNumberToObject(payload, "rappel_minutes", rappel_minutes < 1 ? 1 : rappel_minutes);
    if (lieu != NULL && lieu[0] != '\0') {
        cJSON_AddStringToObject(payload, "lieu", lieu);
    }
    if (notes != NULL && notes[0] != '\0') {
        cJSON_AddStringToObject(payload, "notes", notes);
    }

    payload_json = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (payload_json == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de sérialiser le rendez-vous médical.");
    }

    snprintf(url, sizeof(url), "%s/rest/v1/rendez_vous", supabase_url);
    headers = build_supabase_headers(api_key_copy, 1);
    response = perform_curl_request(url, "POST", headers, payload_json, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);
    free(payload_json);

    if (response == NULL) {
        return make_error_json("Impossible d'ajouter le rendez-vous médical.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    parsed = cJSON_Parse(response);
    if (parsed == NULL) {
        return response;
    }

    if (cJSON_IsArray(parsed) && cJSON_GetArraySize(parsed) > 0) {
        formatted = cJSON_PrintUnformatted(cJSON_GetArrayItem(parsed, 0));
        cJSON_Delete(parsed);
        free(response);
        return formatted != NULL ? formatted : make_error_json("Impossible de formater le rendez-vous médical.");
    }

    cJSON_Delete(parsed);
    return response;
}

static char *supabase_delete_rendezvous(const char *rendezvous_id) {
    char supabase_url[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    char *response = NULL;
    long status_code = 0;

    trim_trailing_slashes(get_env_or_empty("SUPABASE_URL"), supabase_url, sizeof(supabase_url));
    if (supabase_url[0] == '\0') {
        return make_error_json("SUPABASE_URL manquant.");
    }

    api_key_copy = duplicate_string(get_env_or_empty("SUPABASE_ANON_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("SUPABASE_ANON_KEY manquant.");
    }

    snprintf(url, sizeof(url), "%s/rest/v1/rendez_vous?id=eq.%s", supabase_url, rendezvous_id);
    headers = build_supabase_headers(api_key_copy, 0);
    response = perform_curl_request(url, "DELETE", headers, NULL, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);

    if (response != NULL) {
        free(response);
    }

    if (status_code < 200 || status_code >= 300) {
        return make_error_json("Impossible de supprimer le rendez-vous médical.");
    }

    return make_message_json("message", "Rendez-vous supprimé.");
}

static char *gemini_ask(const char *question) {
    char gemini_key[1024];
    char url[2048];
    char *api_key_copy = NULL;
    struct curl_slist *headers = NULL;
    cJSON *root = NULL;
    cJSON *contents = NULL;
    cJSON *content = NULL;
    cJSON *parts = NULL;
    cJSON *part = NULL;
    cJSON *generation_config = NULL;
    char *payload_json = NULL;
    char *response = NULL;
    long status_code = 0;
    char *answer = NULL;
    cJSON *response_json = NULL;
    cJSON *candidates = NULL;
    cJSON *candidate = NULL;
    cJSON *candidate_content = NULL;
    cJSON *candidate_parts = NULL;
    cJSON *candidate_part = NULL;
    cJSON *text = NULL;

    api_key_copy = duplicate_string(get_env_or_empty("GEMINI_API_KEY"));
    if (api_key_copy == NULL || api_key_copy[0] == '\0') {
        free(api_key_copy);
        return make_error_json("GEMINI_API_KEY manquant.");
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de préparer la requête Gemini.");
    }

    contents = cJSON_AddArrayToObject(root, "contents");
    content = cJSON_CreateObject();
    parts = cJSON_AddArrayToObject(content, "parts");
    part = cJSON_CreateObject();
    generation_config = cJSON_CreateObject();

    if (contents == NULL || content == NULL || parts == NULL || part == NULL || generation_config == NULL) {
        cJSON_Delete(root);
        free(api_key_copy);
        return make_error_json("Impossible de construire la requête Gemini.");
    }

    cJSON_AddStringToObject(part, "text", question);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddStringToObject(content, "role", "user");
    cJSON_AddItemToArray(contents, content);
    cJSON_AddNumberToObject(generation_config, "temperature", 0.2);
    cJSON_AddNumberToObject(generation_config, "maxOutputTokens", 512);
    cJSON_AddItemToObject(root, "generationConfig", generation_config);

    payload_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload_json == NULL) {
        free(api_key_copy);
        return make_error_json("Impossible de sérialiser la requête Gemini.");
    }

    trim_trailing_slashes("https://generativelanguage.googleapis.com", gemini_key, sizeof(gemini_key));
    snprintf(url, sizeof(url), "%s%s?key=%s", gemini_key, GEMINI_MODEL_PATH, api_key_copy);

    headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    response = perform_curl_request(url, "POST", headers, payload_json, &status_code);
    curl_slist_free_all(headers);
    free(api_key_copy);
    free(payload_json);

    if (response == NULL) {
        return make_error_json("Impossible de contacter Gemini.");
    }

    if (status_code < 200 || status_code >= 300) {
        char *error = make_error_json(response);
        free(response);
        return error;
    }

    response_json = cJSON_Parse(response);
    if (response_json == NULL) {
        answer = make_message_json("response", response);
        free(response);
        return answer;
    }

    candidates = cJSON_GetObjectItemCaseSensitive(response_json, "candidates");
    if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        candidate = cJSON_GetArrayItem(candidates, 0);
        if (cJSON_IsObject(candidate)) {
            candidate_content = cJSON_GetObjectItemCaseSensitive(candidate, "content");
            if (cJSON_IsObject(candidate_content)) {
                candidate_parts = cJSON_GetObjectItemCaseSensitive(candidate_content, "parts");
                if (cJSON_IsArray(candidate_parts) && cJSON_GetArraySize(candidate_parts) > 0) {
                    candidate_part = cJSON_GetArrayItem(candidate_parts, 0);
                    if (cJSON_IsObject(candidate_part)) {
                        text = cJSON_GetObjectItemCaseSensitive(candidate_part, "text");
                        if (cJSON_IsString(text) && text->valuestring != NULL) {
                            answer = make_message_json("response", text->valuestring);
                        }
                    }
                }
            }
        }
    }

    if (answer == NULL) {
        char *fallback_text = extract_text_from_gemini_response(response);
        answer = make_message_json("response", fallback_text);
        free(fallback_text);
    }

    cJSON_Delete(response_json);
    free(response);
    return answer;
}

static int handle_request(socket_t client, char *request) {
    char method[16] = {0};
    char raw_path[256] = {0};
    char path[256] = {0};
    const char *query = NULL;
    char *body = NULL;
    char *response = NULL;
    int matched = 0;

    if (sscanf(request, "%15s %255s", method, raw_path) != 2) {
        send_text_error(client, 400, "Bad Request", "Requête HTTP invalide.");
        return 0;
    }

    split_path_and_query(raw_path, path, sizeof(path), &query);

    if (strcasecmp(method, "OPTIONS") == 0) {
        send_no_content_response(client);
        return 0;
    }

    body = find_request_body(request);
    if (body == NULL) {
        body = "";
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/medicaments") == 0) {
        response = supabase_get_medicaments();
        matched = 1;
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/medicaments") == 0) {
        cJSON *json = cJSON_Parse(body);
        cJSON *nom = NULL;
        cJSON *dose = NULL;
        cJSON *heure = NULL;
        cJSON *photo_url = NULL;
        cJSON *quantite_restante = NULL;
        cJSON *seuil_alerte = NULL;
        cJSON *proche_nom = NULL;
        cJSON *proche_contact = NULL;

        if (json == NULL) {
            send_text_error(client, 400, "Bad Request", "Corps JSON invalide.");
            return 0;
        }

        nom = cJSON_GetObjectItemCaseSensitive(json, "nom");
        dose = cJSON_GetObjectItemCaseSensitive(json, "dose");
        heure = cJSON_GetObjectItemCaseSensitive(json, "heure");
        photo_url = cJSON_GetObjectItemCaseSensitive(json, "photo_url");
        quantite_restante = cJSON_GetObjectItemCaseSensitive(json, "quantite_restante");
        seuil_alerte = cJSON_GetObjectItemCaseSensitive(json, "seuil_alerte");
        proche_nom = cJSON_GetObjectItemCaseSensitive(json, "proche_nom");
        proche_contact = cJSON_GetObjectItemCaseSensitive(json, "proche_contact");

        if (!cJSON_IsString(nom) || nom->valuestring == NULL ||
            !cJSON_IsString(dose) || dose->valuestring == NULL ||
            !cJSON_IsString(heure) || heure->valuestring == NULL) {
            cJSON_Delete(json);
            send_text_error(client, 400, "Bad Request", "Les champs nom, dose et heure sont obligatoires.");
            return 0;
        }

        response = supabase_insert_medicament(
            nom->valuestring,
            dose->valuestring,
            heure->valuestring,
            cJSON_IsString(photo_url) ? photo_url->valuestring : "",
            json_number_or_default(quantite_restante, 0),
            json_number_or_default(seuil_alerte, 2),
            cJSON_IsString(proche_nom) ? proche_nom->valuestring : "",
            cJSON_IsString(proche_contact) ? proche_contact->valuestring : "");
        cJSON_Delete(json);
        matched = 1;
    } else if (strcmp(method, "PATCH") == 0 && strncmp(path, "/medicaments/", 13) == 0) {
        const char *suffix = strstr(path + 13, "/stock");
        if (suffix != NULL && strcmp(suffix, "/stock") == 0) {
            cJSON *json = cJSON_Parse(body);
            cJSON *quantite = NULL;
            char medicament_id[64] = {0};

            if (json == NULL) {
                send_text_error(client, 400, "Bad Request", "Corps JSON invalide.");
                return 0;
            }

            {
                size_t id_len = (size_t)(suffix - (path + 13));
                if (id_len >= sizeof(medicament_id)) {
                    id_len = sizeof(medicament_id) - 1;
                }
                memcpy(medicament_id, path + 13, id_len);
                medicament_id[id_len] = '\0';
            }

            quantite = cJSON_GetObjectItemCaseSensitive(json, "quantite_restante");
            if (!cJSON_IsNumber(quantite)) {
                cJSON_Delete(json);
                send_text_error(client, 400, "Bad Request", "Le champ quantite_restante est obligatoire.");
                return 0;
            }

            response = supabase_update_stock(medicament_id, quantite->valueint);
            cJSON_Delete(json);
            matched = 1;
        }
    } else if (strcmp(method, "DELETE") == 0 && strncmp(path, "/medicaments/", 13) == 0) {
        const char *id = path + 13;
        if (*id == '\0') {
            send_text_error(client, 400, "Bad Request", "Identifiant manquant.");
            return 0;
        }
        response = supabase_delete_medicament(id);
        matched = 1;
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/prises") == 0) {
        char medicament_id[64] = {0};
        if (query_get_value(query, "medicament_id", medicament_id, sizeof(medicament_id))) {
            response = supabase_get_prises(medicament_id);
        } else {
            response = supabase_get_prises(NULL);
        }
        matched = 1;
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/prises") == 0) {
        cJSON *json = cJSON_Parse(body);
        cJSON *medicament_id = NULL;
        cJSON *statut = NULL;
        cJSON *commentaire = NULL;
        char medicament_id_text[64];

        if (json == NULL) {
            send_text_error(client, 400, "Bad Request", "Corps JSON invalide.");
            return 0;
        }

        medicament_id = cJSON_GetObjectItemCaseSensitive(json, "medicament_id");
        statut = cJSON_GetObjectItemCaseSensitive(json, "statut");
        commentaire = cJSON_GetObjectItemCaseSensitive(json, "commentaire");

        if (!cJSON_IsNumber(medicament_id)) {
            cJSON_Delete(json);
            send_text_error(client, 400, "Bad Request", "Le champ medicament_id est obligatoire.");
            return 0;
        }

        snprintf(medicament_id_text, sizeof(medicament_id_text), "%d", medicament_id->valueint);
        response = supabase_insert_prise(
            medicament_id_text,
            cJSON_IsString(statut) ? statut->valuestring : "prise_confirmee",
            cJSON_IsString(commentaire) ? commentaire->valuestring : "");
        cJSON_Delete(json);
        matched = 1;
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/stats") == 0) {
        response = supabase_get_stats();
        matched = 1;
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/partage-rappel") == 0) {
        cJSON *json = cJSON_Parse(body);
        cJSON *medicament_id = NULL;
        cJSON *destinataire = NULL;
        cJSON *message = NULL;
        char medicament_id_text[64];

        if (json == NULL) {
            send_text_error(client, 400, "Bad Request", "Corps JSON invalide.");
            return 0;
        }

        medicament_id = cJSON_GetObjectItemCaseSensitive(json, "medicament_id");
        destinataire = cJSON_GetObjectItemCaseSensitive(json, "destinataire");
        message = cJSON_GetObjectItemCaseSensitive(json, "message");

        if (!cJSON_IsNumber(medicament_id) || !cJSON_IsString(destinataire) || destinataire->valuestring == NULL ||
            !cJSON_IsString(message) || message->valuestring == NULL) {
            cJSON_Delete(json);
            send_text_error(client, 400, "Bad Request", "Les champs medicament_id, destinataire et message sont obligatoires.");
            return 0;
        }

        snprintf(medicament_id_text, sizeof(medicament_id_text), "%d", medicament_id->valueint);
        response = supabase_insert_partage_rappel(medicament_id_text, destinataire->valuestring, message->valuestring);
        cJSON_Delete(json);
        matched = 1;
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/rendezvous") == 0) {
        response = supabase_get_rendezvous();
        matched = 1;
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/rendezvous") == 0) {
        cJSON *json = cJSON_Parse(body);
        cJSON *titre = NULL;
        cJSON *medecin = NULL;
        cJSON *date_heure = NULL;
        cJSON *lieu = NULL;
        cJSON *notes = NULL;
        cJSON *rappel_minutes = NULL;

        if (json == NULL) {
            send_text_error(client, 400, "Bad Request", "Corps JSON invalide.");
            return 0;
        }

        titre = cJSON_GetObjectItemCaseSensitive(json, "titre");
        medecin = cJSON_GetObjectItemCaseSensitive(json, "medecin");
        date_heure = cJSON_GetObjectItemCaseSensitive(json, "date_heure");
        lieu = cJSON_GetObjectItemCaseSensitive(json, "lieu");
        notes = cJSON_GetObjectItemCaseSensitive(json, "notes");
        rappel_minutes = cJSON_GetObjectItemCaseSensitive(json, "rappel_minutes");

        if (!cJSON_IsString(titre) || titre->valuestring == NULL ||
            !cJSON_IsString(medecin) || medecin->valuestring == NULL ||
            !cJSON_IsString(date_heure) || date_heure->valuestring == NULL) {
            cJSON_Delete(json);
            send_text_error(client, 400, "Bad Request", "Les champs titre, medecin et date_heure sont obligatoires.");
            return 0;
        }

        response = supabase_insert_rendezvous(
            titre->valuestring,
            medecin->valuestring,
            date_heure->valuestring,
            cJSON_IsString(lieu) ? lieu->valuestring : "",
            cJSON_IsString(notes) ? notes->valuestring : "",
            json_number_or_default(rappel_minutes, 60));

        cJSON_Delete(json);
        matched = 1;
    } else if (strcmp(method, "DELETE") == 0 && strncmp(path, "/rendezvous/", 11) == 0) {
        const char *id = path + 11;
        if (*id == '\0') {
            send_text_error(client, 400, "Bad Request", "Identifiant rendez-vous manquant.");
            return 0;
        }
        response = supabase_delete_rendezvous(id);
        matched = 1;
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/chat") == 0) {
        cJSON *json = cJSON_Parse(body);
        cJSON *question = NULL;

        if (json == NULL) {
            send_text_error(client, 400, "Bad Request", "Corps JSON invalide.");
            return 0;
        }

        question = cJSON_GetObjectItemCaseSensitive(json, "question");
        if (!cJSON_IsString(question) || question->valuestring == NULL || question->valuestring[0] == '\0') {
            cJSON_Delete(json);
            send_text_error(client, 400, "Bad Request", "Le champ question est obligatoire.");
            return 0;
        }

        response = gemini_ask(question->valuestring);
        cJSON_Delete(json);
        matched = 1;
    }

    if (!matched) {
        send_text_error(client, 404, "Not Found", "Route introuvable.");
        return 0;
    }

    if (response == NULL) {
        send_text_error(client, 500, "Internal Server Error", "Une erreur interne est survenue.");
        return 0;
    }

    send_json_response(client, 200, "OK", response);
    free(response);
    return 0;
}

static socket_t create_server_socket(int port) {
    socket_t server_socket;
    struct sockaddr_in server_address;
    int reuse_value = 1;

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return INVALID_SOCKET;
    }
#endif

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse_value, sizeof(reuse_value));

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons((unsigned short)port);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        close_socket(server_socket);
        return INVALID_SOCKET;
    }

    if (listen(server_socket, 16) == SOCKET_ERROR) {
        close_socket(server_socket);
        return INVALID_SOCKET;
    }

    return server_socket;
}

int run_server(int port) {
    socket_t server_socket = create_server_socket(port);

    if (server_socket == INVALID_SOCKET) {
        fprintf(stderr, "Impossible de démarrer le serveur sur le port %d.\n", port);
        return 1;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "Impossible d'initialiser libcurl.\n");
        close_socket(server_socket);
        return 1;
    }

    printf("Medicament backend listening on http://localhost:%d\n", port);

    for (;;) {
        struct sockaddr_in client_address;
#ifdef _WIN32
        int client_length = sizeof(client_address);
#else
        socklen_t client_length = sizeof(client_address);
#endif
        socket_t client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_length);
        char *request = NULL;

        if (client_socket == INVALID_SOCKET) {
            continue;
        }

        request = read_full_request(client_socket);
        if (request != NULL) {
            handle_request(client_socket, request);
            free(request);
        } else {
            send_text_error(client_socket, 400, "Bad Request", "Requête incomplète ou vide.");
        }

        close_socket(client_socket);
    }

    curl_global_cleanup();
    close_socket(server_socket);
    return 0;
}
