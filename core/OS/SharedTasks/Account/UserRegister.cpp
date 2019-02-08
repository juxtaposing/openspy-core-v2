#include <OS/OpenSpy.h>
#include <OS/Net/NetPeer.h>
#include <OS/SharedTasks/tasks.h>
#include "ProfileTasks.h"
#include "UserTasks.h"
namespace TaskShared {
    bool PerformUserRegisterRequest(UserRequest request, TaskThreadData *thread_data) {
		curl_data recv_data;
		json_t *root = NULL;
		OS::User user;
		OS::Profile profile;
		//build json object
		json_t *send_obj = json_object();
		
		json_object_set_new(send_obj, "user", OS::UserToJson(request.search_params));

		json_object_set_new(send_obj, "profile", OS::ProfileToJson(request.profile_params));

		json_object_set_new(send_obj, "password", json_string(request.search_params.password.c_str()));

		char *json_data = json_dumps(send_obj, 0);

		CURL *curl = curl_easy_init();
		CURLcode res;
		TaskShared::UserRegisterData register_data;
		
		std::string url = std::string(OS::g_webServicesURL) + "/v1/User/register";

		if (curl) {
			
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

			/* set default user agent */
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "OSUser");

			/* set timeout */
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);

			/* enable location redirects */
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

			/* set maximum allowed redirects */
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 1);

			/* Close socket after one use */
			curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1);

			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&recv_data);

			struct curl_slist *chunk = NULL;
			std::string apiKey = "APIKey: " + std::string(OS::g_webServicesAPIKey);
			chunk = curl_slist_append(chunk, apiKey.c_str());
			chunk = curl_slist_append(chunk, "Content-Type: application/json");
			chunk = curl_slist_append(chunk, std::string(std::string("X-OpenSpy-App: ") + OS::g_appName).c_str());
			if (request.peer) {
				chunk = curl_slist_append(chunk, std::string(std::string("X-OpenSpy-Peer-Address: ") + request.peer->getAddress().ToString()).c_str());
			}
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

			res = curl_easy_perform(curl);
			if (res == CURLE_OK) {
				root = json_loads(recv_data.buffer.c_str(), 0, NULL);
				if (!Handle_WebError(root, register_data.error_details)) {
					json_t *json_obj;
					json_obj = json_object_get(root, "user");
					if (json_obj) {
						register_data.user = OS::LoadUserFromJson(json_obj);
					}

					json_obj = json_object_get(root, "profile");
					if (json_obj) {
						register_data.profile = OS::LoadProfileFromJson(json_obj);
					}
				}
			}
			else {
				register_data.error_details.response_code = TaskShared::WebErrorCode_BackendError;
			}
			curl_easy_cleanup(curl);
		}

		if (root) {
			json_decref(root);
		}

		if (json_data)
			free((void *)json_data);

		if (send_obj)
			json_decref(send_obj);


		if (request.registerCallback != NULL)
			request.registerCallback(register_data.error_details.response_code == TaskShared::WebErrorCode_Success, register_data.user, register_data.profile, register_data, request.extra, request.peer);
		return true;
    }
}