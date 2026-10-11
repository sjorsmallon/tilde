#include "navmesh.hpp"



// provide a list of target points for pathfinding, given a navemesh and a start and end point.
std::vector<linalg::vec3> find_path(const navmesh_t &nav, const linalg::vec3 &start, const linalg::vec3 &end);
