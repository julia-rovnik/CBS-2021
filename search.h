#ifndef SEARCH_H
#define SEARCH_H

#include "map.h"
#include "agent.h"
#include "ctNode.h"
#include "searchNode.h"
#include "pairVert.h"

#include <map>
#include <set>
#include <list>
#include <cmath>
#include <vector>

// a data structure for vertex constraints
typedef std::vector<std::set<std::pair<int, int>>> VertexConstrStruct;
// a data structure for edge constraints
typedef std::vector<std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>> EdgeConstrStruct;
// a data structure for paths (vector if pairs (i, j))
typedef std::vector<std::pair<int, int>> Path;
// a data structure for pairs (state, set of agents that visited it at some point)
typedef std::unordered_map<KeyThree, std::set<int>, KeyHash, KeyEqual> StateMap;

// a comparator for A* nodes
bool CompareAStar(const SearchNode& one, const SearchNode& two);
// a comparator for Focal Search
bool CompareFocal(const SearchNode& one, const SearchNode& two);


class Search {
    public:
        std::vector<SearchNode> partPath;
        Path fullPath;

        Search(Map& map);

        bool checkVertexConstr(int i, int j, int t, VertexConstrStruct& vertexConstr);
        bool checkEdgeConstr(int i1, int j1, int i2, int j2, int time, EdgeConstrStruct& edgeConstr);
        double computeHFromCellToCell(Map& map, int i1, int j1, std::vector<int> i2, std::vector<int> j2, int label, bool dijkstra, std::map<pairVert, int, pvCompare>& distMap);
        void startSearch(Map& map, std::map<pairVert, int, pvCompare>& distMap, VertexConstrStruct& vertexConstr, EdgeConstrStruct& edgeConstr, ConfMap& conflictAvoidanceTable, Agent& agent, StateMap& states, bool useDijkstra, bool useFocal, double omega);
        std::list<SearchNode> findSuccessors(SearchNode& curNode, Map& map, VertexConstrStruct& vertexConstr, EdgeConstrStruct& edgeConstr, Agent& agent, bool dijkstra, std::map<pairVert, int, pvCompare>& distMap);
        void makePartPath(SearchNode curNode, SearchNode startNode);
        void makeFullPath();

        double diagonal(Map& map, int i1, int j1, int i2, int j2);
        double manhattan(Map& map, int i1, int j1, int i2, int j2);
        double euclidean(Map& map, int i1, int j1, int i2, int j2);
        double chebyshev(Map& map, int i1, int j1, int i2, int j2);
};

#endif