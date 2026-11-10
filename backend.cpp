#include "httplib.h"
#include <iostream>
#include <queue>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <limits>
#include <functional>
using namespace std;
struct Emergency {
    int id;
    string patientName;
    string type;
    int severity;
    string pickup;
    string hospital;
    int priorityDistance;
    int sequence;
};
struct CompareEmergency {
    bool operator()(const Emergency& a,
                    const Emergency& b) const {
        if (a.severity != b.severity)
            return a.severity < b.severity;
        if (a.priorityDistance != b.priorityDistance)
            return a.priorityDistance > b.priorityDistance;
        return a.sequence > b.sequence;
    }
};
struct Ambulance {
    int id;
    string location;
    bool available;
};
struct ActiveDispatch {
    Emergency emergency;
    int ambulanceId;
    vector<string> route;
    int routeDistance;
    vector<string> hospitalRoute;
    int hospitalDistance;
};
struct Road {
    int destination;
    int distance;
};
struct RouteResult {
    int distance;
    vector<int> path;
    bool reachable;
};
priority_queue<Emergency,
               vector<Emergency>,
               CompareEmergency> emergencyQueue;
vector<Ambulance> ambulances;
vector<ActiveDispatch> activeDispatches;
vector<Emergency> completedEmergencies;
vector<string> sectors = {
    "Sector 15",
    "Sector 17",
    "Sector 22",
    "Sector 34",
    "Sector 35",
    "Sector 43",
    "Sector 8",
    "Sector 9",
    "Sector 11",
    "Sector 12",
    "Sector 16",
    "Sector 23"
};
vector<vector<Road>> graph(12);
unordered_map<string, int> sectorIndex;
mutex stateMutex;
int nextSequence = 1;
void addRoad(int u, int v, int distance) {
    graph[u].push_back({v, distance});
    graph[v].push_back({u, distance});
}
void initializeCity() {
    for (int i = 0; i < (int)sectors.size(); i++) {
        sectorIndex[sectors[i]] = i;
    }
    addRoad(0, 1, 3);
    addRoad(0, 2, 4);
    addRoad(0, 6, 5);
    addRoad(1, 2, 3);
    addRoad(1, 3, 5);
    addRoad(1, 10, 3);
    addRoad(2, 3, 4);
    addRoad(2, 11, 2);
    addRoad(3, 4, 2);
    addRoad(3, 5, 5);
    addRoad(3, 10, 3);
    addRoad(4, 5, 4);
    addRoad(4, 11, 3);
    addRoad(5, 11, 5);
    addRoad(6, 7, 2);
    addRoad(6, 8, 4);
    addRoad(7, 8, 2);
    addRoad(7, 9, 4);
    addRoad(8, 9, 2);
    addRoad(8, 10, 4);
    addRoad(9, 10, 5);
    addRoad(10, 11, 4);
    addRoad(9, 5, 2);
    addRoad(6, 1, 7);
}
RouteResult dijkstra(int source, int destination) {
    const int INF = numeric_limits<int>::max();
    vector<int> dist(sectors.size(), INF);
    vector<int> parent(sectors.size(), -1);
    priority_queue<
        pair<int, int>,
        vector<pair<int, int>>,
        greater<pair<int, int>>
    > pq;
    dist[source] = 0;
    pq.push({0, source});
    while (!pq.empty()) {
        int currentDistance = pq.top().first;
        int u = pq.top().second;
        pq.pop();
        if (currentDistance > dist[u])
            continue;
        if (u == destination)
            break;
        for (const Road& road : graph[u]) {
            int v = road.destination;
            int weight = road.distance;
            if (dist[u] != INF &&
                dist[u] + weight < dist[v]) {
                dist[v] = dist[u] + weight;
                parent[v] = u;
                pq.push({dist[v], v});
            }
        }
    }
    RouteResult result;
    result.reachable = dist[destination] != INF;
    result.distance = result.reachable ? dist[destination] : -1;
    if (!result.reachable)
        return result;
    for (int v = destination; v != -1; v = parent[v]) {
        result.path.push_back(v);
    }
    reverse(result.path.begin(), result.path.end());
    return result;
}
string jsonEscape(const string& input) {
    string output;
    for (char c : input) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 32)
                    output += ' ';
                else
                    output += c;
        }
    }
    return output;
}
string jsonString(const string& value) {
    return "\"" + jsonEscape(value) + "\"";
}
string getParam(const httplib::Params& params,
                const string& key) {
    auto it = params.find(key);
    if (it != params.end())
        return it->second;
    return "";
}
bool parseInteger(const string& value, int& result) {
    if (value.empty())
        return false;
    try {
        size_t pos = 0;
        int parsed = stoi(value, &pos);
        if (pos != value.size())
            return false;
        result = parsed;
        return true;
    } catch (...) {
        return false;
    }
}
string readFile(const string& path) {
    ifstream file(path);
    if (!file.is_open())
        return "<h1>Unable to load dashboard.</h1>";
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
string pathToJSON(const vector<int>& path) {
    string result = "[";
    for (size_t i = 0; i < path.size(); i++) {
        if (i > 0)
            result += ",";
        result += jsonString(sectors[path[i]]);
    }
    result += "]";
    return result;
}
bool emergencyIdExists(int id) {
    auto copy = emergencyQueue;
    while (!copy.empty()) {
        if (copy.top().id == id)
            return true;
        copy.pop();
    }
    for (const auto& dispatch : activeDispatches) {
        if (dispatch.emergency.id == id)
            return true;
    }
    for (const auto& emergency : completedEmergencies) {
        if (emergency.id == id)
            return true;
    }
    return false;
}
struct AmbulanceChoice {
    int index = -1;
    int distance = -1;
    vector<int> path;
};
AmbulanceChoice findNearestAmbulance(const string& pickup) {
    AmbulanceChoice best;
    int destination = sectorIndex.at(pickup);
    for (int i = 0; i < (int)ambulances.size(); i++) {
        if (!ambulances[i].available)
            continue;
        int source = sectorIndex.at(ambulances[i].location);
        RouteResult route = dijkstra(source, destination);
        if (!route.reachable)
            continue;
        if (best.index == -1 ||
            route.distance < best.distance) {
            best.index = i;
            best.distance = route.distance;
            best.path = route.path;
        }
    }
    return best;
}

void refreshQueuePriorities() {
    vector<Emergency> pending;

    while (!emergencyQueue.empty()) {
        pending.push_back(emergencyQueue.top());
        emergencyQueue.pop();
    }

    for (auto& emergency : pending) {
        AmbulanceChoice nearest = findNearestAmbulance(emergency.pickup);
        emergency.priorityDistance =
            nearest.index == -1
                ? numeric_limits<int>::max()
                : nearest.distance;
        emergencyQueue.push(emergency);
    }
}
string buildStateJSON() {
    string json = "{";
    json += "\"stats\":{";
    json += "\"availableAmbulances\":" +
            to_string(count_if(
                ambulances.begin(),
                ambulances.end(),
                [](const Ambulance& a) {
                    return a.available;
                }
            )) + ",";
    json += "\"busyAmbulances\":" +
            to_string(ambulances.size() -
                count_if(
                    ambulances.begin(),
                    ambulances.end(),
                    [](const Ambulance& a) {
                        return a.available;
                    }
                )) + ",";
    json += "\"pendingEmergencies\":" +
            to_string(emergencyQueue.size()) + ",";
    json += "\"activeEmergencies\":" +
            to_string(activeDispatches.size()) + ",";
    json += "\"completedEmergencies\":" +
            to_string(completedEmergencies.size());
    json += "},";
    json += "\"ambulances\":[";
    for (size_t i = 0; i < ambulances.size(); i++) {
        if (i) json += ",";
        const auto& a = ambulances[i];
        json += "{";
        json += "\"id\":" + to_string(a.id) + ",";
        json += "\"location\":" + jsonString(a.location) + ",";
        json += "\"available\":" +
                string(a.available ? "true" : "false");
        json += "}";
    }
    json += "],";
    auto copy = emergencyQueue;
    json += "\"pending\":[";
    bool first = true;
    while (!copy.empty()) {
        Emergency e = copy.top();
        copy.pop();
        if (!first) json += ",";
        first = false;
        json += "{";
        json += "\"id\":" + to_string(e.id) + ",";
        json += "\"patientName\":" + jsonString(e.patientName) + ",";
        json += "\"type\":" + jsonString(e.type) + ",";
        json += "\"severity\":" + to_string(e.severity) + ",";
        json += "\"pickup\":" + jsonString(e.pickup) + ",";
        json += "\"hospital\":" + jsonString(e.hospital) + ",";
        json += "\"distance\":" + to_string(e.priorityDistance);
        json += "}";
    }
    json += "],";
    json += "\"active\":[";
    for (size_t i = 0; i < activeDispatches.size(); i++) {
        if (i) json += ",";
        const auto& d = activeDispatches[i];
        json += "{";
        json += "\"id\":" + to_string(d.emergency.id) + ",";
        json += "\"patientName\":" +
                jsonString(d.emergency.patientName) + ",";
        json += "\"type\":" + jsonString(d.emergency.type) + ",";
        json += "\"severity\":" +
                to_string(d.emergency.severity) + ",";
        json += "\"pickup\":" + jsonString(d.emergency.pickup) + ",";
        json += "\"hospital\":" + jsonString(d.emergency.hospital) + ",";
        json += "\"ambulanceId\":" + to_string(d.ambulanceId) + ",";
        json += "\"routeDistance\":" + to_string(d.routeDistance) + ",";
        json += "\"route\":[";
        for (size_t j = 0; j < d.route.size(); j++) {
            if (j) json += ",";
            json += jsonString(d.route[j]);
        }
        json += "],";
        json += "\"hospitalDistance\":" + to_string(d.hospitalDistance) + ",";
        json += "\"hospitalRoute\":[";
        for (size_t j = 0; j < d.hospitalRoute.size(); j++) {
            if (j) json += ",";
            json += jsonString(d.hospitalRoute[j]);
        }
        json += "]}";
    }
    json += "],";
    json += "\"completed\":[";
    for (size_t i = 0; i < completedEmergencies.size(); i++) {
        if (i) json += ",";
        const auto& e = completedEmergencies[i];
        json += "{";
        json += "\"id\":" + to_string(e.id) + ",";
        json += "\"patientName\":" + jsonString(e.patientName) + ",";
        json += "\"type\":" + jsonString(e.type) + ",";
        json += "\"severity\":" + to_string(e.severity) + ",";
        json += "\"pickup\":" + jsonString(e.pickup) + ",";
        json += "\"hospital\":" + jsonString(e.hospital);
        json += "}";
    }
    json += "],";
    json += "\"nodes\":[";
    for (size_t i = 0; i < sectors.size(); i++) {
        if (i) json += ",";
        json += jsonString(sectors[i]);
    }
    json += "],";
    json += "\"roads\":[";
    bool firstRoad = true;
    for (int i = 0; i < (int)graph.size(); i++) {
        for (const Road& road : graph[i]) {
            if (i >= road.destination)
                continue;
            if (!firstRoad) json += ",";
            firstRoad = false;
            json += "{";
            json += "\"from\":" + jsonString(sectors[i]) + ",";
            json += "\"to\":" + jsonString(sectors[road.destination]) + ",";
            json += "\"distance\":" + to_string(road.distance);
            json += "}";
        }
    }
    json += "]}";
    return json;
}
void initializeAmbulances() {
    ambulances = {
        {101, "Sector 15", true},
        {102, "Sector 17", true},
        {103, "Sector 22", true},
        {104, "Sector 34", true},
        {105, "Sector 43", true},
        {106, "Sector 12", true},
        {107, "Sector 8", true},
        {108, "Sector 23", true}
    };
}
int main() {
    initializeCity();
    initializeAmbulances();
    httplib::Server svr;
    svr.Get("/", [](const httplib::Request&,
                    httplib::Response& res) {
        res.set_content(readFile("index.html"), "text/html");
    });
    svr.Get("/api/state", [](const httplib::Request&,
                             httplib::Response& res) {
        lock_guard<mutex> lock(stateMutex);
        refreshQueuePriorities();
        res.set_content(buildStateJSON(), "application/json");
    });
    svr.Post("/api/emergencies", [](const httplib::Request& req,
                                    httplib::Response& res) {
        httplib::Params params;
        httplib::detail::parse_query_text(req.body, params);
        int id, severity;
        string patient = getParam(params, "patientName");
        string type = getParam(params, "type");
        string pickup = getParam(params, "pickup");
        string hospital = getParam(params, "hospital");
        if (!parseInteger(getParam(params, "id"), id) ||
            !parseInteger(getParam(params, "severity"), severity) ||
            patient.empty() ||
            type.empty() ||
            pickup.empty() ||
            hospital.empty()) {
            res.status = 400;
            res.set_content(
                R"({"error":"Please provide all required fields."})",
                "application/json"
            );
            return;
        }
        lock_guard<mutex> lock(stateMutex);
        if (id <= 0 || severity < 1 || severity > 5 ||
            patient.size() > 100 ||
            type.size() > 50 ||
            sectorIndex.find(pickup) == sectorIndex.end() ||
            sectorIndex.find(hospital) == sectorIndex.end()) {
            res.status = 400;
            res.set_content(
                R"({"error":"Invalid ID, severity, text length or location."})",
                "application/json"
            );
            return;
        }
        if (emergencyIdExists(id)) {
            res.status = 409;
            res.set_content(
                R"({"error":"Emergency ID already exists."})",
                "application/json"
            );
            return;
        }
        AmbulanceChoice nearest = findNearestAmbulance(pickup);
        int distance = nearest.index == -1
                       ? numeric_limits<int>::max()
                       : nearest.distance;
        Emergency emergency = {
            id,
            patient,
            type,
            severity,
            pickup,
            hospital,
            distance,
            nextSequence++
        };
        emergencyQueue.push(emergency);
        res.set_content(
            R"({"success":true,"message":"Emergency registered successfully."})",
            "application/json"
        );
    });
    svr.Post("/api/dispatch", [](const httplib::Request&,
                                 httplib::Response& res) {
        lock_guard<mutex> lock(stateMutex);
        refreshQueuePriorities();

        if (emergencyQueue.empty()) {
            res.status = 400;
            res.set_content(
                R"({"error":"No pending emergencies."})",
                "application/json"
            );
            return;
        }
        Emergency next = emergencyQueue.top();
        AmbulanceChoice choice = findNearestAmbulance(next.pickup);
        if (choice.index == -1) {
            res.status = 409;
            res.set_content(
                R"({"error":"No available ambulances. Emergency remains in queue."})",
                "application/json"
            );
            return;
        }
        emergencyQueue.pop();
        Ambulance& ambulance = ambulances[choice.index];
        ambulance.available = false;
        vector<string> route;
        for (int node : choice.path) {
            route.push_back(sectors[node]);
        }
        RouteResult hospitalLeg = dijkstra(
            sectorIndex.at(next.pickup),
            sectorIndex.at(next.hospital)
        );
        vector<string> hospitalRoute;
        for (int node : hospitalLeg.path) {
            hospitalRoute.push_back(sectors[node]);
        }
        ActiveDispatch dispatch = {
            next,
            ambulance.id,
            route,
            choice.distance,
            hospitalRoute,
            hospitalLeg.reachable ? hospitalLeg.distance : -1
        };
        activeDispatches.push_back(dispatch);
        string json = "{";
        json += "\"success\":true,";
        json += "\"emergencyId\":" + to_string(next.id) + ",";
        json += "\"ambulanceId\":" + to_string(ambulance.id) + ",";
        json += "\"distance\":" + to_string(choice.distance) + ",";
        json += "\"route\":" + pathToJSON(choice.path);
        json += "}";
        res.set_content(json, "application/json");
    });
    svr.Post("/api/complete", [](const httplib::Request& req,
                                 httplib::Response& res) {
        httplib::Params params;
        httplib::detail::parse_query_text(req.body, params);
        int ambulanceId;
        if (!parseInteger(
                getParam(params, "ambulanceId"),
                ambulanceId)) {
            res.status = 400;
            res.set_content(
                R"({"error":"Invalid ambulance ID."})",
                "application/json"
            );
            return;
        }
        lock_guard<mutex> lock(stateMutex);
        auto it = find_if(
            activeDispatches.begin(),
            activeDispatches.end(),
            [ambulanceId](const ActiveDispatch& d) {
                return d.ambulanceId == ambulanceId;
            }
        );
        if (it == activeDispatches.end()) {
            res.status = 404;
            res.set_content(
                R"({"error":"No active dispatch found for this ambulance."})",
                "application/json"
            );
            return;
        }
        auto ambulance = find_if(
            ambulances.begin(),
            ambulances.end(),
            [ambulanceId](const Ambulance& a) {
                return a.id == ambulanceId;
            }
        );
        if (ambulance != ambulances.end()) {
            ambulance->available = true;
            ambulance->location = it->emergency.hospital;
        }
        completedEmergencies.push_back(it->emergency);
        activeDispatches.erase(it);
        res.set_content(
            R"({"success":true,"message":"Emergency completed. Ambulance is available again."})",
            "application/json"
        );
    });
    svr.Post("/api/reset", [](const httplib::Request&,
                              httplib::Response& res) {
        lock_guard<mutex> lock(stateMutex);
        emergencyQueue = decltype(emergencyQueue)();
        activeDispatches.clear();
        completedEmergencies.clear();
        initializeAmbulances();
        nextSequence = 1;
        res.set_content(
            R"({"success":true,"message":"Simulation reset successfully."})",
            "application/json"
        );
    });
    int port = 8080;
    if (const char* envPort = getenv("PORT")) {
        try {
            port = stoi(envPort);
        } catch (...) {
            cerr << "Invalid PORT environment variable.\n";
            return 1;
        }
    }
    if (port < 1 || port > 65535) {
        cerr << "Invalid server port.\n";
        return 1;
    }
    cout << "\n========================================\n";
    cout << " EMERGENCY AMBULANCE DISPATCH SIMULATOR\n";
    cout << "========================================\n";
    cout << " Server running on port: " << port << "\n";
    cout << "========================================\n";
    if (!svr.listen("0.0.0.0", port)) {
        cerr << "Failed to start server.\n";
        return 1;
    }
    return 0;
}
