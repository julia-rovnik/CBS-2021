#include "map.h"
#include "agent.h"
#include "searchNode.h"
#include "search.h"
#include "constr.h"
#include "ctNode.h"
#include "ctSolution.h"
#include "pairVert.h"

#include <cmath>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <iostream>
#include <cstdlib>
#include <climits>

CTSolution::CTSolution(Map& map_read, std::vector<Agent>& agents_read, bool useDijkstraPrecalc_read, bool useCAT_read, std::string heuristic_read, bool prioritizeConflicts_read, bool useBypass_read, bool useFocal_read, double omega_read, bool useSymmetry_read,  bool online_read, std::vector<std::vector<std::pair<int, int>>>& goalLocs_read, int horizon_read, int replanning_read, bool printPaths_read) {
    map = map_read;
    agents = agents_read;
    useDijkstraPrecalc = useDijkstraPrecalc_read;
    useCAT = useCAT_read;
    heuristic = heuristic_read;
    prioritizeConflicts = prioritizeConflicts_read;
    useBypass = useBypass_read;
    useFocal = useFocal_read;
    omega = omega_read;
    useSymmetry = useSymmetry_read;
    online = online_read;
    goalLocs = goalLocs_read;
    horizon = horizon_read;
    replanning = replanning_read;
    printPaths = printPaths_read;
}

std::map<pairVert, int, pvCompare> CTSolution::dijkstraPrecalc(Map& map) {
    int INF = 1000 * 1000 * 1000;
    int h = map.height;
    int w = map.width;

    std::map<pairVert, int, pvCompare> d;

    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++ j) {
            if (map.cellOnGrid(i, j) && map.cellIsTraversable(i, j)) {
                // for each empty cell on the grid
                // find distances to all other cells

                pairVert s;
                s.from = {i, j};
                s.to = {i, j};

                d[s] = 0;

                std::set<std::pair<int, std::pair<int, int>>> q;
                q.insert({d[s], {i, j}});

                while (!q.empty()) {
                    std::pair<int, int> v = q.begin()->second;
                    q.erase(q.begin());

                    for (int plus_i = -1; plus_i <= 1; ++plus_i) {
                        for (int plus_j = -1; plus_j <= 1; ++plus_j) {
                            if ((plus_i != 0 || plus_j != 0) && !(plus_i != 0 && plus_j != 0)) {
                                if (map.cellOnGrid(v.first + plus_i, v.second + plus_j) && map.cellIsTraversable(v.first + plus_i, v.second + plus_j)) {

                                    // found an empty neighbour cell
                                    // add metric type later
                                    std::pair<int, int> to = {v.first + plus_i, v.second + plus_j};
                                    int len = 1;

                                    pairVert s_v;
                                    s_v.from = {i, j};
                                    s_v.to = v;

                                    if (d.find(s_v) == d.end()) {
                                        d[s_v] = INF;
                                    }

                                    pairVert s_to;
                                    s_to.from = {i, j};
                                    s_to.to = to;

                                    if (d.find(s_to) == d.end()) {
                                        d[s_to] = INF;
                                    }

                                    // relaxation
                                    if (d[s_v] + len < d[s_to]) {
                                        q.erase({d[s_to], to});
                                        d[s_to] = d[s_v] + len;
                                        q.insert({d[s_to], to});
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    return d;
}

Path CTSolution::lowLevelSearch(CTNode node, int i) {
    // create a search object
    // it uses search options (type of search, constraints etc) and returns paths
    // the paths may include conflicts
    Search search(map);
    search.startSearch(map, distMap, node.vertexConstr, node.edgeConstr, node.conflictAvoidanceTable, agents[i], node.stateAgentMap, useDijkstraPrecalc, useFocal, omega);
    return search.fullPath;
}

std::vector<Path> CTSolution::highLevelSearch() {
    // create the root of the CT with paths made by low-level search
    CTNode root;
    for (int agent = 0; agent < agents.size(); ++agent) {
        root.paths.push_back(lowLevelSearch(root, agent));
    }
    // count the cost of the current solution
    root.countCost(heuristic);

    if (useCAT) {
        root.countCAT();
    }

    // add the root node to the heap
    // nodes are sorted by solution cost
    heap.push(root);

    // while there are nodes to analyze, take the best node and analyze it
    while (!heap.empty()) {
        // extract the best node from the CT
        CTNode best;
        best = heap.top();
        heap.pop();

        // find the best conflict in this node
        Conflict conflict = best.findBestConflict(map, useDijkstraPrecalc, distMap, agents, prioritizeConflicts, useSymmetry, heuristic, horizon);

        // if there are no conflicts, we have found the goal node
        if (conflict.type == "none") {
            return best.paths;
        }

        // some variables for bypass
        bool bypassFound = false;
        std::vector<CTNode> twoNodes;
        int curConfNum, curPathCost, newConfNum, newPathCost;

        // if there is a conflict, we construct constraints on this conflict
        // two agents take part in a conflict: agent_number == 0 and agent_number == 1
        for (int agent_number = 0; agent_number < 2; ++agent_number) {
            int agent; // the agent
            int confTime; // when did the conflict happen

            if (agent_number == 0) {
                agent = (conflict.agents).first;
                confTime = conflict.time1;
            } else {
                agent = (conflict.agents).second;
                confTime = conflict.time2;
            }

            // create a new node
            // it inherits vertex and edge constraints from the best node
            CTNode node;
            node.vertexConstr = best.vertexConstr;
            node.edgeConstr = best.edgeConstr;

            // we are only going to change one agent's path
            // other paths are inherited from the best node
            node.paths = best.paths;
            if (useCAT) {
                node.countCAT();
            }

            if (useBypass) {
                // count old path cost and number of conflicts involving one agent
                curConfNum = node.findNumOfConflicts(map, agents, agent, horizon);
                curPathCost = node.countPathCost(agent, heuristic);
            }

            // with a rectangular vertex conflict, we need to insert a barrier constraint
            // we count Rk and Rg and prohibit the agent ak from occupying all locations along the border of the rectangle
            // that is opposite of its start node at the timestep when ak would optimally reach the location
            if (conflict.type == "rectangular") {
                int a1 = (conflict.agents).first;
                int a2 = (conflict.agents).second;

                int start_ix = agents[a1].start_i;
                int start_iy = agents[a1].start_j;
                int goal_ix = agents[a1].fin_i[0];
                int goal_iy = agents[a1].fin_j[0];

                int start_jx = agents[a2].start_i;
                int start_jy = agents[a2].start_j;
                int goal_jx = agents[a2].fin_i[0];
                int goal_jy = agents[a2].fin_j[0];

                int start_it = 0;
                int goal_it = node.paths[a1].size() - 1;
                int start_jt = 0;
                int goal_jt = node.paths[a2].size() - 1;

                int R_sx, R_gx, R_sy, R_gy, R_ix, R_jx, R_iy, R_jy;
                int R_it, R_jt, R_st, R_gt;

                if (start_ix == goal_ix) {
                    R_sx = start_ix;
                    R_gx = goal_ix;
                } else if (start_ix < goal_ix) {
                    R_sx = std::max(start_ix, start_jx);
                    R_gx = std::min(goal_ix, goal_jx);
                } else {
                    R_sx = std::min(start_ix, start_jx);
                    R_gx = std::max(goal_ix, goal_jx);
                }

                if (start_iy == goal_iy) {
                    R_sy = start_iy;
                    R_gy = goal_iy;
                } else if (start_iy < goal_iy) {
                    R_sy = std::max(start_iy, start_jy);
                    R_gy = std::min(goal_iy, goal_jy);
                } else {
                    R_sy = std::min(start_iy, start_jy);
                    R_gy = std::max(goal_iy, goal_jy);
                }

                if ((start_ix - start_jx) * (start_jx - R_gx) >= 0) {
                    R_ix = R_gx;
                    R_iy = start_iy;
                    R_jx = start_jx;
                    R_jy = R_gy;
                } else {
                    R_ix = start_ix;
                    R_iy = R_gy;
                    R_jx = R_gx;
                    R_jy = start_jy;
                }

                R_it = start_it + std::abs(start_ix - R_ix) + std::abs(start_iy - R_iy);
                R_jt = start_it + std::abs(start_ix - R_jx) + std::abs(start_iy - R_jy);
                R_st = start_it + std::abs(start_ix - R_sx) + std::abs(start_iy - R_sy);
                R_gt = start_it + std::abs(start_ix - R_gx) + std::abs(start_iy - R_gy);

                if (agent_number == 1) {
                    if (R_ix == R_gx) {
                        for (int n = R_it; n <= R_gt; ++n) {
                            node.vertexConstr[n].insert({R_ix, R_iy + n - R_it});
                        }
                    } else if (R_iy == R_gy) {
                        for (int n = R_it; n <= R_gt; ++n) {
                            node.vertexConstr[n].insert({R_ix + n - R_it, R_iy});
                        }
                    }
                } else {
                    if (R_jx == R_gx) {
                        for (int n = R_jt; n <= R_gt; ++n) {
                            node.vertexConstr[n].insert({R_jx, R_jy + n - R_jt});
                        }
                    } else if (R_jy == R_gy) {
                        for (int n = R_jt; n <= R_gt; ++n) {
                            node.vertexConstr[n].insert({R_jx + n - R_jt, R_jy});
                        }
                    }
                }


            // with a vertex conflict, we only need to insert a constraint that doesn't allow an agent
            // to be in a certain place at a certain time
            } else if (conflict.type == "vertex") {
                node.vertexConstr[confTime].insert(conflict.v1);
            
            // with an edge conflict, we insert a conflict tuple
            // the pairs correspond to the vertices at the ends of the edge
            } else if (conflict.type == "edge") {
                std::pair<std::pair<int, int>, std::pair<int, int>> confTuple;
                if (agent_number == 0) {
                    confTuple = {{conflict.v1.first, conflict.v1.second}, {conflict.v2.first, conflict.v2.second}};
                    node.edgeConstr[conflict.time1].insert(confTuple);
                } else {
                    confTuple = {{conflict.v2.first, conflict.v2.second}, {conflict.v1.first, conflict.v1.second}};
                    node.edgeConstr[conflict.time2].insert(confTuple);
                }
            }

            // we find a new path for this agent
            node.paths[agent] = lowLevelSearch(node, agent);
            // and count the node's cost
            node.countCost(heuristic);

            if (useBypass) {
                newConfNum = node.findNumOfConflicts(map, agents, agent, horizon);
                newPathCost = node.countPathCost(agent, heuristic);

                // if the cost does not change and confNum decreases, we have found a helpful bypass!
                // we adopt it and don't split the node
                if (newPathCost == curPathCost && newConfNum < curConfNum) {
                    bypassFound = true;
                    heap.push(node);
                    break;
                // otherwise, store this node in a vector
                } else {
                    twoNodes.push_back(node);
                }
            }

            if (!useBypass) {
                heap.push(node);
            }
        }

        // if there were no helpful bypasses, we just split the node
        if (useBypass && !bypassFound) {
            heap.push(twoNodes[0]);
            heap.push(twoNodes[1]);
        }
    }

    // if we didn't find a solution
    return root.paths;
}

void CTSolution::solve() {
    // precompute exact heuristic
    if (useDijkstraPrecalc) {
        distMap = dijkstraPrecalc(map);
    }

    if (!online) {
        std::vector<Path> paths = highLevelSearch();

        int cost = 0;
        for (int i = 0; i < paths.size(); ++i) {
            cost += paths[i].size() - 1;
        }
        //std::cout << "Solution Cost: " << cost << "\n";

        // if option printPaths is activated, we print out the path of each agent
        if (printPaths) {
            for (int agent = 0; agent < paths.size(); ++agent) {
                std::cout << "agent " << agent + 1 << ": [ ";
                for (int i = 0; i < paths[agent].size(); ++i) {
                    auto currentPair = paths[agent][i];
                    std::cout << "[" << currentPair.first << ", " << currentPair.second << "] ";
                }
                std::cout << "]\n";
            }
        }

        return;
    }

    std::vector<std::vector<std::pair<int, int>>> constructedPaths(agents.size(), std::vector<std::pair<int, int>>());

    // true = the agent completed all its tasks
    std::vector<bool> done(agents.size(), false);
    // how many goals has this agent visited
    std::vector<int> last(agents.size(), 0);
    // how many agents did this
    int progress = 0;

    while (progress < agents.size()) {
        // assign goals to agents
        for (int agent = 0; agent < agents.size(); ++agent) {
            if (!done[agent]) {
                int num = 0;
                int dist = 0;
                int cur_i = agents[agent].start_i;
                int cur_j = agents[agent].start_j;

                // first lets count min dist between assigned goals
                while (num < agents[agent].fin_i.size()) {
                    int gi = agents[agent].fin_i[num];
                    int gj = agents[agent].fin_j[num];
                    dist += std::abs(cur_i - gi) + std::abs(cur_j - gj);
                    cur_i = gi;
                    cur_j = gj;
                    ++num;
                }

                // next lets add goals until (dist > replanning)
                num = last[agent];
                while (dist < replanning && num < goalLocs[agent].size()) {
                    int gi = goalLocs[agent][num].first;
                    int gj = goalLocs[agent][num].second;

                    agents[agent].fin_i.push_back(gi);
                    agents[agent].fin_j.push_back(gj);

                    dist += std::abs(cur_i - gi) * map.hweight + std::abs(cur_j - gj);
                    cur_i = gi;
                    cur_j = gj;
                    ++num;
                }
            }
        }

        // find paths
        std::vector<Path> paths = highLevelSearch();

        for (int agent = 0; agent < agents.size(); ++agent) {
            if (!done[agent]) {
                int step = 0;
                int label = 0;
                while (step < replanning && step < paths[agent].size()) {
                    constructedPaths[agent].push_back(paths[agent][step]);
                    if (paths[agent][step].first == agents[agent].fin_i[label] && paths[agent][step].second == agents[agent].fin_j[label]) {
                        // at this moment the agent visited one of the goals
                        ++label;
                        ++last[agent];
                        // if the agent visited all its goals
                        if (last[agent] == goalLocs[agent].size()) {
                            done[agent] = true;
                            ++progress;
                            break;
                        }
                    }
                    ++step;
                }
                // update the starting point
                if (!done[agent]) {
                    agents[agent].start_i = paths[agent][step].first;
                    agents[agent].start_j = paths[agent][step].second;
                    agents[agent].fin_i.clear();
                    agents[agent].fin_j.clear();
                }
            }
        }
    }

    int cost = 0;
    for (int i = 0; i < constructedPaths.size(); ++i) {
        cost += constructedPaths[i].size() - 1;
    }
    //std::cout << "Solution Cost: " << cost << "\n";

    if (printPaths) {
        for (int agent = 0; agent < agents.size(); ++agent) {
            std::cout << "agent " << agent + 1 << ": [ ";
            for (int i = 0; i < constructedPaths[agent].size(); ++i) {
                auto currentPair = constructedPaths[agent][i];
                std::cout << "[" << currentPair.first << ", " << currentPair.second << "] ";
            }
            std::cout << "]\n";
        }
    }
}
