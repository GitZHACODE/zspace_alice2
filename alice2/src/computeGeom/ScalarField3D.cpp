#include "scalarField3D.h"
#include "../objects/MeshObject.h"
#include "../core/Renderer.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace alice2 {

    // Marching cubes edge table - maps vertex configuration to intersected edges
    const int EDGE_TABLE[256] = {
        0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
        0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
        0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
        0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
        0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
        0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
        0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
        0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
        0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
        0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
        0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
        0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
        0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
        0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
        0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
        0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
        0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
        0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
        0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
        0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
        0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
        0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
        0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
        0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
        0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
        0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
        0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
        0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
        0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
        0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
        0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
        0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
    };

    // Complete marching cubes triangle table - all 256 entries
    const int TRI_TABLE[256][16] = {
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
        {3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
        {3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
        {3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
        {9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
        {9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
        {2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
        {8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
        {9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
        {4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
        {3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
        {1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1},
        {4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
        {4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
        {9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
        {5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
        {2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
        {9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
        {0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
        {2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
        {10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
        {4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
        {5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1},
        {5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
        {9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
        {0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
        {1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
        {10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
        {8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
        {2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
        {7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
        {9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
        {2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
        {11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
        {9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
        {5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
        {11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
        {11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
        {1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
        {9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
        {5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
        {2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
        {0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
        {5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
        {6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
        {3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
        {6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
        {5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
        {1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
        {10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1},
        {6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
        {8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
        {7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1},
        {3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
        {5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
        {0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
        {9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
        {8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
        {5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
        {0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
        {6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
        {10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
        {10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
        {8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1},
        {1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
        {3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
        {0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
        {10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
        {3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
        {6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
        {9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1},
        {8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1},
        {3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1},
        {6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
        {0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
        {10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
        {10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
        {2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1},
        {7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
        {7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
        {2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
        {1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
        {11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
        {8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
        {0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
        {7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
        {10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
        {2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
        {6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1},
        {7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1},
        {2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1},
        {1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1},
        {10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
        {10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1},
        {0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1},
        {7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1},
        {6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
        {8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1},
        {9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1},
        {6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1},
        {4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1},
        {10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1},
        {8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1},
        {0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1},
        {1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1},
        {8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1},
        {10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1},
        {4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1},
        {10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
        {5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
        {11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1},
        {9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
        {6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1},
        {7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1},
        {3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1},
        {7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1},
        {9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1},
        {3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1},
        {6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1},
        {9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1},
        {1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1},
        {4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1},
        {7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1},
        {6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1},
        {3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
        {0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1},
        {6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1},
        {0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1},
        {11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1},
        {6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1},
        {5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1},
        {9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1},
        {1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1},
        {1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1},
        {10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1},
        {0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1},
        {5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1},
        {10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1},
        {11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1},
        {9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1},
        {7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1},
        {2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1},
        {8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1},
        {9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1},
        {9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1},
        {1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1},
        {9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1},
        {9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1},
        {5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1},
        {0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1},
        {10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1},
        {2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1},
        {0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1},
        {0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1},
        {9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1},
        {5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1},
        {3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1},
        {5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1},
        {8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1},
        {0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1},
        {9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1},
        {0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1},
        {1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
        {8, 3, 1, 8, 1, 4, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1},
        {4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1},
        {9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1},
        {11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1},
        {11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1},
        {2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1},
        {9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1},
        {3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1},
        {1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1},
        {4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1},
        {4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1},
        {0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1},
        {3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1},
        {3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1},
        {0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1},
        {9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1},
        {1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
    };

    // Constructor
    ScalarField3D::ScalarField3D(const Vec3& min_bb, const Vec3& max_bb, int res_x, int res_y, int res_z)
        : m_min_bounds(min_bb), m_max_bounds(max_bb), m_res_x(res_x), m_res_y(res_y), m_res_z(res_z) {
        if (res_x <= 0 || res_y <= 0 || res_z <= 0) {
            throw std::invalid_argument("Resolution must be positive");
        }

        const int total_points = m_res_x * m_res_y * m_res_z;
        m_grid_points.reserve(total_points);
        m_field_values.resize(total_points, 0.0f);
        m_normalized_values.resize(total_points, 0.0f);

        initialize_grid();
    }

    // Copy constructor
    ScalarField3D::ScalarField3D(const ScalarField3D& other)
        : m_min_bounds(other.m_min_bounds), m_max_bounds(other.m_max_bounds)
        , m_res_x(other.m_res_x), m_res_y(other.m_res_y), m_res_z(other.m_res_z)
        , m_grid_points(other.m_grid_points)
        , m_field_values(other.m_field_values)
        , m_normalized_values(other.m_normalized_values) {
    }

    // Copy assignment operator
    ScalarField3D& ScalarField3D::operator=(const ScalarField3D& other) {
        if (this != &other) {
            m_min_bounds = other.m_min_bounds;
            m_max_bounds = other.m_max_bounds;
            m_res_x = other.m_res_x;
            m_res_y = other.m_res_y;
            m_res_z = other.m_res_z;
            m_grid_points = other.m_grid_points;
            m_field_values = other.m_field_values;
            m_normalized_values = other.m_normalized_values;
        }
        return *this;
    }

    // Move constructor
    ScalarField3D::ScalarField3D(ScalarField3D&& other) noexcept
        : m_min_bounds(other.m_min_bounds), m_max_bounds(other.m_max_bounds)
        , m_res_x(other.m_res_x), m_res_y(other.m_res_y), m_res_z(other.m_res_z)
        , m_grid_points(std::move(other.m_grid_points))
        , m_field_values(std::move(other.m_field_values))
        , m_normalized_values(std::move(other.m_normalized_values)) {
    }

    // Move assignment operator
    ScalarField3D& ScalarField3D::operator=(ScalarField3D&& other) noexcept {
        if (this != &other) {
            m_min_bounds = other.m_min_bounds;
            m_max_bounds = other.m_max_bounds;
            m_res_x = other.m_res_x;
            m_res_y = other.m_res_y;
            m_res_z = other.m_res_z;
            m_grid_points = std::move(other.m_grid_points);
            m_field_values = std::move(other.m_field_values);
            m_normalized_values = std::move(other.m_normalized_values);
        }
        return *this;
    }

    bool ScalarField3D::is_inside_bounds(const Vec3& p) const {
        return p.x >= m_min_bounds.x && p.x <= m_max_bounds.x &&
               p.y >= m_min_bounds.y && p.y <= m_max_bounds.y &&
               p.z >= m_min_bounds.z && p.z <= m_max_bounds.z;
    }

    Vec3 ScalarField3D::clamp_to_bounds(const Vec3& p) const {
        return Vec3(
            std::clamp(p.x, m_min_bounds.x, m_max_bounds.x),
            std::clamp(p.y, m_min_bounds.y, m_max_bounds.y),
            std::clamp(p.z, m_min_bounds.z, m_max_bounds.z)
        );
    }

    void ScalarField3D::initialize_grid() {
        m_grid_points.clear();
        
        const Vec3 step = Vec3(
            (m_max_bounds.x - m_min_bounds.x) / (m_res_x - 1),
            (m_max_bounds.y - m_min_bounds.y) / (m_res_y - 1),
            (m_max_bounds.z - m_min_bounds.z) / (m_res_z - 1)
        );

        for (int k = 0; k < m_res_z; ++k) {
            for (int j = 0; j < m_res_y; ++j) {
                for (int i = 0; i < m_res_x; ++i) {
                    Vec3 point = m_min_bounds + Vec3(i * step.x, j * step.y, k * step.z);
                    m_grid_points.push_back(point);
                }
            }
        }
    }

    void ScalarField3D::normalize_field() {
        if (m_field_values.empty()) return;

        auto [min_it, max_it] = std::minmax_element(m_field_values.begin(), m_field_values.end());
        float min_val = *min_it;
        float max_val = *max_it;
        
        if (std::abs(max_val - min_val) < 1e-6f) {
            std::fill(m_normalized_values.begin(), m_normalized_values.end(), 0.0f);
            return;
        }

        float range = max_val - min_val;
        for (size_t i = 0; i < m_field_values.size(); ++i) {
            m_normalized_values[i] = (m_field_values[i] - min_val) / range;
        }
    }

    void ScalarField3D::set_values(const std::vector<float>& values) {
        if (values.size() != m_field_values.size()) {
            throw std::invalid_argument("Values size must match grid size");
        }
        m_field_values = values;
        normalize_field();
    }

    Vec3 ScalarField3D::cell_position(int x, int y, int z) const {
        if (!is_valid_coords(x, y, z)) {
            throw std::out_of_range("Invalid grid coordinates");
        }
        int index = get_index(x, y, z);
        return m_grid_points[index];
    }

    Vec3 ScalarField3D::get_cell_size() const {
        return Vec3(
            (m_max_bounds.x - m_min_bounds.x) / std::max(1, m_res_x - 1),
            (m_max_bounds.y - m_min_bounds.y) / std::max(1, m_res_y - 1),
            (m_max_bounds.z - m_min_bounds.z) / std::max(1, m_res_z - 1)
        );
    }

    bool ScalarField3D::contains_point(const Vec3& p) const {
        return is_inside_bounds(p);
    }

    float ScalarField3D::sample_nearest(const Vec3& p) const {
        // Convert world position to grid coordinates
        float fx = (p.x - m_min_bounds.x) / (m_max_bounds.x - m_min_bounds.x) * (m_res_x - 1);
        float fy = (p.y - m_min_bounds.y) / (m_max_bounds.y - m_min_bounds.y) * (m_res_y - 1);
        float fz = (p.z - m_min_bounds.z) / (m_max_bounds.z - m_min_bounds.z) * (m_res_z - 1);

        int ix = std::clamp(static_cast<int>(std::round(fx)), 0, m_res_x - 1);
        int iy = std::clamp(static_cast<int>(std::round(fy)), 0, m_res_y - 1);
        int iz = std::clamp(static_cast<int>(std::round(fz)), 0, m_res_z - 1);

        return m_field_values[get_index(ix, iy, iz)];
    }

    void ScalarField3D::clear_field() {
        std::fill(m_field_values.begin(), m_field_values.end(), 0.0f);
        std::fill(m_normalized_values.begin(), m_normalized_values.end(), 0.0f);
    }

    // Apply scalar sphere to field
    void ScalarField3D::apply_scalar_sphere(const Vec3& center, float radius) {
        for (int k = 0; k < m_res_z; ++k) {
            for (int j = 0; j < m_res_y; ++j) {
                for (int i = 0; i < m_res_x; ++i) {
                    const int idx = get_index(i, j, k);
                    const Vec3& pt = m_grid_points[idx];
                    const float distance = (pt - center).length();
                    const float sdf = distance - radius; // SDF: negative inside, positive outside
                    m_field_values[idx] = sdf;
                }
            }
        }
        normalize_field();
    }

    // Apply scalar box to field
    void ScalarField3D::apply_scalar_box(const Vec3& center, const Vec3& half_size) {
        for (int k = 0; k < m_res_z; ++k) {
            for (int j = 0; j < m_res_y; ++j) {
                for (int i = 0; i < m_res_x; ++i) {
                    const int idx = get_index(i, j, k);
                    const Vec3& pt = m_grid_points[idx];
                    const Vec3 d = Vec3(
                        std::abs(pt.x - center.x) - half_size.x,
                        std::abs(pt.y - center.y) - half_size.y,
                        std::abs(pt.z - center.z) - half_size.z
                    );
                    const float sdf = std::max({d.x, d.y, d.z, 0.0f}) +
                                     Vec3(std::max(d.x, 0.0f), std::max(d.y, 0.0f), std::max(d.z, 0.0f)).length();
                    m_field_values[idx] = sdf;
                }
            }
        }
        normalize_field();
    }

    // Apply scalar torus to field
    void ScalarField3D::apply_scalar_torus(const Vec3& center, float major_radius, float minor_radius) {
        for (int k = 0; k < m_res_z; ++k) {
            for (int j = 0; j < m_res_y; ++j) {
                for (int i = 0; i < m_res_x; ++i) {
                    const int idx = get_index(i, j, k);
                    const Vec3& pt = m_grid_points[idx];
                    const Vec3 offset = pt - center;
                    const float q = std::sqrt(offset.x * offset.x + offset.y * offset.y) - major_radius;
                    const float sdf = std::sqrt(q * q + offset.z * offset.z) - minor_radius;
                    m_field_values[idx] = sdf;
                }
            }
        }
        normalize_field();
    }

    // Apply scalar plane to field
    void ScalarField3D::apply_scalar_plane(const Vec3& point, const Vec3& normal) {
        Vec3 norm = normal.normalized();
        for (int k = 0; k < m_res_z; ++k) {
            for (int j = 0; j < m_res_y; ++j) {
                for (int i = 0; i < m_res_x; ++i) {
                    const int idx = get_index(i, j, k);
                    const Vec3& pt = m_grid_points[idx];
                    const float sdf = (pt - point).dot(norm);
                    m_field_values[idx] = sdf;
                }
            }
        }
        normalize_field();
    }

    // Apply scalar noise to field
    void ScalarField3D::apply_scalar_noise(float frequency, float amplitude) {
        for (int k = 0; k < m_res_z; ++k) {
            for (int j = 0; j < m_res_y; ++j) {
                for (int i = 0; i < m_res_x; ++i) {
                    const int idx = get_index(i, j, k);
                    const Vec3& pt = m_grid_points[idx];
                    // Simple noise based on position
                    float noise = std::sin(pt.x * frequency) * std::sin(pt.y * frequency) * std::sin(pt.z * frequency);
                    m_field_values[idx] = noise * amplitude;
                }
            }
        }
        normalize_field();
    }

    // Boolean operations - simplified versions
    void ScalarField3D::boolean_union(const ScalarField3D& other) {
        if (m_field_values.size() != other.m_field_values.size()) {
            return; // Skip if sizes don't match
        }

        for (size_t i = 0; i < m_field_values.size(); ++i) {
            m_field_values[i] = std::min(m_field_values[i], other.m_field_values[i]);
        }
        normalize_field();
    }

    void ScalarField3D::boolean_intersect(const ScalarField3D& other) {
        if (m_field_values.size() != other.m_field_values.size()) {
            return; // Skip if sizes don't match
        }

        for (size_t i = 0; i < m_field_values.size(); ++i) {
            m_field_values[i] = std::max(m_field_values[i], other.m_field_values[i]);
        }
        normalize_field();
    }

    void ScalarField3D::boolean_subtract(const ScalarField3D& other) {
        if (m_field_values.size() != other.m_field_values.size()) {
            return; // Skip if sizes don't match
        }

        for (size_t i = 0; i < m_field_values.size(); ++i) {
            m_field_values[i] = std::max(m_field_values[i], -other.m_field_values[i]);
        }
        normalize_field();
    }

    void ScalarField3D::boolean_smin(const ScalarField3D& other, float smoothing) {
        if (m_field_values.size() != other.m_field_values.size()) {
            return; // Skip if sizes don't match
        }

        for (size_t i = 0; i < m_field_values.size(); ++i) {
            float a = m_field_values[i];
            float b = other.m_field_values[i];
            float r = std::exp2(-a / smoothing) + std::exp2(-b / smoothing);
            m_field_values[i] = -smoothing * std::log2(r);
        }
        normalize_field();
    }

    // Vertex classification for extended marching cubes
    alice2::VertexClass ScalarField3D::classify_vertex(float value, float isolevel, float tolerance) const {
        float diff = value - isolevel;
        if (std::abs(diff) <= tolerance) {
            return alice2::VertexClass::ZERO;
        }
        return (diff > 0) ? alice2::VertexClass::POSITIVE : alice2::VertexClass::NEGATIVE;
    }

    // Original vertex interpolation for marching cubes (kept for compatibility)
    Vec3 ScalarField3D::vertex_interpolate(float isolevel, const Vec3& p1, const Vec3& p2, float val1, float val2) const {
        return vertex_interpolate_robust(isolevel, p1, p2, val1, val2);
    }

    // Robust vertex interpolation for marching cubes with better numerical stability
    Vec3 ScalarField3D::vertex_interpolate_robust(float isolevel, const Vec3& p1, const Vec3& p2, float val1, float val2) const {
        const float tolerance = 1e-6f;

        // Check if isolevel is very close to either vertex value
        if (std::abs(isolevel - val1) < tolerance) return p1;
        if (std::abs(isolevel - val2) < tolerance) return p2;

        // Check for degenerate case where both values are nearly equal
        float value_diff = val2 - val1;
        if (std::abs(value_diff) < tolerance) {
            // Return midpoint for nearly equal values to avoid division by zero
            return (p1 + p2) * 0.5f;
        }

        // Compute interpolation parameter with clamping for numerical stability
        float mu = (isolevel - val1) / value_diff;
        mu = std::clamp(mu, 0.0f, 1.0f);  // Ensure mu stays in valid range

        return p1 + (p2 - p1) * mu;
    }

    // Get grid cell for marching cubes
    GridCell ScalarField3D::get_grid_cell(int x, int y, int z) const {
        GridCell cell;

        // Bounds check - ensure we can access x+1, y+1, z+1
        if (x < 0 || x >= m_res_x - 1 || y < 0 || y >= m_res_y - 1 || z < 0 || z >= m_res_z - 1) {
            // Return empty cell for out of bounds
            for (int i = 0; i < 8; ++i) {
                cell.vertices[i] = Vec3(0, 0, 0);
                cell.values[i] = 0.0f;
                cell.classes[i] = alice2::VertexClass::NEGATIVE;
            }
            return cell;
        }

        // Define the 8 vertices of the cube in standard marching cubes order
        cell.vertices[0] = cell_position(x, y, z);
        cell.vertices[1] = cell_position(x + 1, y, z);
        cell.vertices[2] = cell_position(x + 1, y + 1, z);
        cell.vertices[3] = cell_position(x, y + 1, z);
        cell.vertices[4] = cell_position(x, y, z + 1);
        cell.vertices[5] = cell_position(x + 1, y, z + 1);
        cell.vertices[6] = cell_position(x + 1, y + 1, z + 1);
        cell.vertices[7] = cell_position(x, y + 1, z + 1);

        // Get scalar values at each vertex
        cell.values[0] = m_field_values[get_index(x, y, z)];
        cell.values[1] = m_field_values[get_index(x + 1, y, z)];
        cell.values[2] = m_field_values[get_index(x + 1, y + 1, z)];
        cell.values[3] = m_field_values[get_index(x, y + 1, z)];
        cell.values[4] = m_field_values[get_index(x, y, z + 1)];
        cell.values[5] = m_field_values[get_index(x + 1, y, z + 1)];
        cell.values[6] = m_field_values[get_index(x + 1, y + 1, z + 1)];
        cell.values[7] = m_field_values[get_index(x, y + 1, z + 1)];

        // Initialize vertex classifications (will be set by polygonize_cell)
        for (int i = 0; i < 8; ++i) {
            cell.classes[i] = alice2::VertexClass::NEGATIVE;  // Default, will be updated
        }

        return cell;
    }

    // Check if a triangle is degenerate (has zero or near-zero area)
    bool ScalarField3D::is_triangle_degenerate(const MCTriangle& triangle, float tolerance) const {
        Vec3 v1 = triangle.vertices[1] - triangle.vertices[0];
        Vec3 v2 = triangle.vertices[2] - triangle.vertices[0];
        Vec3 cross = v1.cross(v2);
        return cross.length() < tolerance;
    }

    // Validate triangle quality (area and aspect ratio)
    bool ScalarField3D::validate_triangle_quality(const MCTriangle& triangle, float min_area) const {
        Vec3 v1 = triangle.vertices[1] - triangle.vertices[0];
        Vec3 v2 = triangle.vertices[2] - triangle.vertices[0];
        Vec3 v3 = triangle.vertices[2] - triangle.vertices[1];

        // Check for degenerate triangle (zero area)
        Vec3 cross = v1.cross(v2);
        float area = cross.length() * 0.5f;
        if (area < min_area) {
            return false;
        }

        // Check for extremely thin triangles (aspect ratio check)
        float edge1_len = v1.length();
        float edge2_len = v2.length();
        float edge3_len = v3.length();

        // Find the longest edge
        float max_edge = std::max({edge1_len, edge2_len, edge3_len});

        // Aspect ratio check: area should be reasonable relative to longest edge
        if (max_edge > 0.0f) {
            float aspect_ratio = area / (max_edge * max_edge);
            if (aspect_ratio < 1e-6f) {  // Very thin triangle
                return false;
            }
        }

        return true;
    }

    // Extract triangles using proper marching cubes algorithm
    std::vector<MCTriangle> ScalarField3D::extract_triangles(float isolevel) const {
        std::vector<MCTriangle> triangles;
        int processed_cells = 0;
        int active_cells = 0;

        // Process each cell in the grid
        for (int k = 0; k < m_res_z - 1; ++k) {
            for (int j = 0; j < m_res_y - 1; ++j) {
                for (int i = 0; i < m_res_x - 1; ++i) {
                    GridCell cell = get_grid_cell(i, j, k);
                    int triangles_before = static_cast<int>(triangles.size());
                    polygonize_cell(cell, isolevel, triangles);
                    // polygonize_cell_tetra(cell, isolevel, triangles);
                    int triangles_after = static_cast<int>(triangles.size());

                    processed_cells++;
                    if (triangles_after > triangles_before) {
                        active_cells++;
                    }
                }
            }
        }

        std::cout << "Enhanced Marching Cubes processed " << processed_cells << " cells, "
                  << active_cells << " generated triangles, total triangles: "
                  << triangles.size() << std::endl;

        return triangles;
    }

    // Generate mesh data from scalar field
    std::shared_ptr<MeshData> ScalarField3D::generate_mesh(float isolevel) const {
        auto meshData = std::make_shared<MeshData>();
        std::vector<MCTriangle> triangles = extract_triangles(isolevel);

        // Convert triangles to mesh data
        for (const auto& triangle : triangles) {
            int baseIndex = static_cast<int>(meshData->vertices.size());

            // Add vertices
            for (int i = 0; i < 3; ++i) {
                MeshVertex vertex;
                vertex.position = triangle.vertices[i];
                vertex.normal = triangle.normal;
                vertex.color = Color(0.8f, 0.8f, 0.9f); // Light blue-gray
                meshData->vertices.push_back(vertex);
            }

            // Add face (triangle)
            MeshFace face;
            face.vertices = {baseIndex, baseIndex + 1, baseIndex + 2};
            face.normal = triangle.normal;
            face.color = Color(0.8f, 0.8f, 0.9f);
            meshData->faces.push_back(face);

            // Add edges
            meshData->edges.push_back(MeshEdge(baseIndex, baseIndex + 1));
            meshData->edges.push_back(MeshEdge(baseIndex + 1, baseIndex + 2));
            meshData->edges.push_back(MeshEdge(baseIndex + 2, baseIndex));
        }

        meshData->triangulationDirty = true;
        return meshData;
    }

    // Trilinear interpolation sampling
    float ScalarField3D::sample_trilinear(const Vec3& p) const {
        // Convert world position to grid coordinates
        float fx = (p.x - m_min_bounds.x) / (m_max_bounds.x - m_min_bounds.x) * (m_res_x - 1);
        float fy = (p.y - m_min_bounds.y) / (m_max_bounds.y - m_min_bounds.y) * (m_res_y - 1);
        float fz = (p.z - m_min_bounds.z) / (m_max_bounds.z - m_min_bounds.z) * (m_res_z - 1);

        int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, m_res_x - 2);
        int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, m_res_y - 2);
        int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, m_res_z - 2);

        int x1 = x0 + 1;
        int y1 = y0 + 1;
        int z1 = z0 + 1;

        float tx = fx - x0;
        float ty = fy - y0;
        float tz = fz - z0;

        // Get the 8 corner values
        float c000 = m_field_values[get_index(x0, y0, z0)];
        float c001 = m_field_values[get_index(x0, y0, z1)];
        float c010 = m_field_values[get_index(x0, y1, z0)];
        float c011 = m_field_values[get_index(x0, y1, z1)];
        float c100 = m_field_values[get_index(x1, y0, z0)];
        float c101 = m_field_values[get_index(x1, y0, z1)];
        float c110 = m_field_values[get_index(x1, y1, z0)];
        float c111 = m_field_values[get_index(x1, y1, z1)];

        // Trilinear interpolation
        float c00 = c000 * (1 - tx) + c100 * tx;
        float c01 = c001 * (1 - tx) + c101 * tx;
        float c10 = c010 * (1 - tx) + c110 * tx;
        float c11 = c011 * (1 - tx) + c111 * tx;

        float c0 = c00 * (1 - ty) + c10 * ty;
        float c1 = c01 * (1 - ty) + c11 * ty;

        return c0 * (1 - tz) + c1 * tz;
    }

    // Gradient calculation using central differences
    Vec3 ScalarField3D::gradient_at(const Vec3& p) const {
        float eps = 1.0f;

        float dx = sample_trilinear(Vec3(p.x + eps, p.y, p.z)) - sample_trilinear(Vec3(p.x - eps, p.y, p.z));
        float dy = sample_trilinear(Vec3(p.x, p.y + eps, p.z)) - sample_trilinear(Vec3(p.x, p.y - eps, p.z));
        float dz = sample_trilinear(Vec3(p.x, p.y, p.z + eps)) - sample_trilinear(Vec3(p.x, p.y, p.z - eps));

        return Vec3(dx, dy, dz) * 0.5f;
    }

    float ScalarField3D::value_at(const Vec3& p) const{
        if (m_field_values.empty()) {
            return 0.0f;
        }

        Vec3 samplePoint = contains_point(p) ? p : clamp_to_bounds(p);

        if (m_res_x <= 1 || m_res_y <= 1 || m_res_z <= 1) {
            return sample_nearest(samplePoint);
        }

        return sample_trilinear(samplePoint);
    }

    Vec3 ScalarField3D::gradient_normalized(const Vec3& p) const {
        Vec3 g = gradient_at(p);
        float len = g.length();
        if (len <= 1e-6f) {
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        return g * (1.0f / len);
    }

    Vec3 ScalarField3D::project_onto_isosurface(const Vec3& start, float isoLevel, int maxIterations, float tolerance) const {
        Vec3 p = clamp_to_bounds(start);
        for (int i = 0; i < maxIterations; ++i) {
            float value = sample_trilinear(p);
            float diff = value - isoLevel;
            if (std::abs(diff) <= tolerance) {
                break;
            }
            Vec3 grad = gradient_at(p);
            float gradLenSq = grad.x * grad.x + grad.y * grad.y + grad.z * grad.z;
            if (gradLenSq < 1e-8f) {
                break;
            }
            p = p - grad * (diff / gradLenSq);
            p = clamp_to_bounds(p);
        }
        return p;
    }

    // Rendering methods
    void ScalarField3D::draw_points(Renderer& renderer, int step) const {
        for (int k = 0; k < m_res_z; k += step) {
            for (int j = 0; j < m_res_y; j += step) {
                for (int i = 0; i < m_res_x; i += step) {
                    int idx = get_index(i, j, k);
                    const Vec3& pos = m_grid_points[idx];
                    float value = m_normalized_values[idx];

                    // Color based on field value
                    Color color = Color::lerp(Color(0, 0, 1), Color(1, 0, 0), value);
                    renderer.drawPoint(pos, color, 2.0f);
                }
            }
        }
    }

    void ScalarField3D::draw_values(Renderer& renderer, int step) const {
        renderer.setColor(Color(1, 1, 1)); // Set text color
        for (int k = 0; k < m_res_z; k += step) {
            for (int j = 0; j < m_res_y; j += step) {
                for (int i = 0; i < m_res_x; i += step) {
                    int idx = get_index(i, j, k);
                    const Vec3& pos = m_grid_points[idx];
                    float value = m_field_values[idx];

                    std::string text = std::to_string(static_cast<int>(value * 100) / 100.0f);
                    renderer.drawText(text, pos, 12.0f);
                }
            }
        }
    }

    void ScalarField3D::draw_slice(Renderer& renderer, int z_slice, float point_size) const {
        if (z_slice < 0 || z_slice >= m_res_z) return;

        for (int j = 0; j < m_res_y; ++j) {
            for (int i = 0; i < m_res_x; ++i) {
                int idx = get_index(i, j, z_slice);
                const Vec3& pos = m_grid_points[idx];
                float value = m_normalized_values[idx];

                // Color based on field value
                Color color = Color::lerp(Color(0, 0, 1), Color(1, 0, 0), value);
                renderer.drawPoint(pos, color, point_size);
            }
        }
    }

    // Stub implementations for scalar functions (return simple values)
    float ScalarField3D::get_scalar_sphere(const Vec3& center, float radius) const {
        return radius; // Simple stub
    }

    float ScalarField3D::get_scalar_box(const Vec3& center, const Vec3& half_size) const {
        return half_size.x; // Simple stub
    }

    float ScalarField3D::get_scalar_torus(const Vec3& center, float major_radius, float minor_radius) const {
        return major_radius; // Simple stub
    }

    float ScalarField3D::get_scalar_plane(const Vec3& point, const Vec3& normal) const {
        return 0.0f; // Simple stub
    }

    // Enhanced marching cubes polygonize cell implementation with robust vertex classification
    int ScalarField3D::polygonize_cell(const GridCell& cell, float isolevel, std::vector<MCTriangle>& triangles) const {
        int cubeindex = 0;
        Vec3 vertlist[12];

        // Classify vertices using enhanced three-state classification
        alice2::VertexClass vertex_classes[8];
        bool has_zero_vertices = false;

        for (int i = 0; i < 8; ++i) {
            vertex_classes[i] = classify_vertex(cell.values[i], isolevel);
            if (vertex_classes[i] == alice2::VertexClass::ZERO) {
                has_zero_vertices = true;
            }
        }

        // Build cube index using enhanced classification
        // For vertices exactly on the isosurface (ZERO), treat as positive to avoid degeneracies
        if (vertex_classes[0] != alice2::VertexClass::NEGATIVE) cubeindex |= 1;
        if (vertex_classes[1] != alice2::VertexClass::NEGATIVE) cubeindex |= 2;
        if (vertex_classes[2] != alice2::VertexClass::NEGATIVE) cubeindex |= 4;
        if (vertex_classes[3] != alice2::VertexClass::NEGATIVE) cubeindex |= 8;
        if (vertex_classes[4] != alice2::VertexClass::NEGATIVE) cubeindex |= 16;
        if (vertex_classes[5] != alice2::VertexClass::NEGATIVE) cubeindex |= 32;
        if (vertex_classes[6] != alice2::VertexClass::NEGATIVE) cubeindex |= 64;
        if (vertex_classes[7] != alice2::VertexClass::NEGATIVE) cubeindex |= 128;

        // Bounds check for cube index
        if (cubeindex < 0 || cubeindex >= 256) return 0;

        // Cube is entirely in/out of the surface
        if (EDGE_TABLE[cubeindex] == 0) return 0;

        // Find vertices where the surface intersects the cube edges using robust interpolation
        if (EDGE_TABLE[cubeindex] & 1)
            vertlist[0] = vertex_interpolate_robust(isolevel, cell.vertices[0], cell.vertices[1], cell.values[0], cell.values[1]);
        if (EDGE_TABLE[cubeindex] & 2)
            vertlist[1] = vertex_interpolate_robust(isolevel, cell.vertices[1], cell.vertices[2], cell.values[1], cell.values[2]);
        if (EDGE_TABLE[cubeindex] & 4)
            vertlist[2] = vertex_interpolate_robust(isolevel, cell.vertices[2], cell.vertices[3], cell.values[2], cell.values[3]);
        if (EDGE_TABLE[cubeindex] & 8)
            vertlist[3] = vertex_interpolate_robust(isolevel, cell.vertices[3], cell.vertices[0], cell.values[3], cell.values[0]);
        if (EDGE_TABLE[cubeindex] & 16)
            vertlist[4] = vertex_interpolate_robust(isolevel, cell.vertices[4], cell.vertices[5], cell.values[4], cell.values[5]);
        if (EDGE_TABLE[cubeindex] & 32)
            vertlist[5] = vertex_interpolate_robust(isolevel, cell.vertices[5], cell.vertices[6], cell.values[5], cell.values[6]);
        if (EDGE_TABLE[cubeindex] & 64)
            vertlist[6] = vertex_interpolate_robust(isolevel, cell.vertices[6], cell.vertices[7], cell.values[6], cell.values[7]);
        if (EDGE_TABLE[cubeindex] & 128)
            vertlist[7] = vertex_interpolate_robust(isolevel, cell.vertices[7], cell.vertices[4], cell.values[7], cell.values[4]);
        if (EDGE_TABLE[cubeindex] & 256)
            vertlist[8] = vertex_interpolate_robust(isolevel, cell.vertices[0], cell.vertices[4], cell.values[0], cell.values[4]);
        if (EDGE_TABLE[cubeindex] & 512)
            vertlist[9] = vertex_interpolate_robust(isolevel, cell.vertices[1], cell.vertices[5], cell.values[1], cell.values[5]);
        if (EDGE_TABLE[cubeindex] & 1024)
            vertlist[10] = vertex_interpolate_robust(isolevel, cell.vertices[2], cell.vertices[6], cell.values[2], cell.values[6]);
        if (EDGE_TABLE[cubeindex] & 2048)
            vertlist[11] = vertex_interpolate_robust(isolevel, cell.vertices[3], cell.vertices[7], cell.values[3], cell.values[7]);

        // Create triangles using the triangle table with enhanced quality validation
        int ntriang = 0;
        for (int i = 0; i < 16 && TRI_TABLE[cubeindex][i] != -1; i += 3) {
            // Bounds check for triangle indices
            if (i + 2 >= 16) break;

            int idx0 = TRI_TABLE[cubeindex][i];
            int idx1 = TRI_TABLE[cubeindex][i + 1];
            int idx2 = TRI_TABLE[cubeindex][i + 2];

            // Validate vertex indices
            if (idx0 < 0 || idx0 >= 12 || idx1 < 0 || idx1 >= 12 || idx2 < 0 || idx2 >= 12) {
                continue;
            }

            MCTriangle triangle;
            triangle.vertices[0] = vertlist[idx0];
            triangle.vertices[1] = vertlist[idx1];
            triangle.vertices[2] = vertlist[idx2];

            // Enhanced triangle quality validation
            if (is_triangle_degenerate(triangle)) {
                continue;  // Skip degenerate triangles
            }

            if (!validate_triangle_quality(triangle)) {
                continue;  // Skip poor quality triangles
            }

            // Calculate normal with improved robustness
            Vec3 v1 = triangle.vertices[1] - triangle.vertices[0];
            Vec3 v2 = triangle.vertices[2] - triangle.vertices[0];
            Vec3 normal = v1.cross(v2);

            float length = normal.length();
            if (length > 1e-8f) {  // More strict threshold for normal calculation
                triangle.normal = normal / length;

                // Ensure consistent winding order for better topology
                // (This helps prevent T-junction issues between adjacent cells)
                if (has_zero_vertices) {
                    // For cells with vertices on the isosurface, be extra careful with orientation
                    // Check if normal points in consistent direction
                    Vec3 cell_center = (cell.vertices[0] + cell.vertices[1] + cell.vertices[2] + cell.vertices[3] +
                                       cell.vertices[4] + cell.vertices[5] + cell.vertices[6] + cell.vertices[7]) * 0.125f;
                    Vec3 triangle_center = (triangle.vertices[0] + triangle.vertices[1] + triangle.vertices[2]) / 3.0f;
                    Vec3 to_center = cell_center - triangle_center;

                    // Flip normal if it points toward the cell center (inside)
                    if (triangle.normal.dot(to_center) > 0.0f) {
                        // triangle.normal = -triangle.normal;
                        std::swap(triangle.vertices[1], triangle.vertices[2]);
                    }
                }

                triangles.push_back(triangle);
                ntriang++;
            }
        }

        // Debug output for quality improvements
        // static int debug_counter = 0;
        // if (++debug_counter % 1000 == 0) {  // Print every 1000 cells
        //     std::cout << "Enhanced MC: Cell " << debug_counter
        //               << ", triangles generated: " << ntriang
        //               << ", zero vertices: " << (has_zero_vertices ? "yes" : "no") << std::endl;
        // }

        return ntriang;
    }

    // Marching tetrahedra
    // 6 tetrahedra per cube, expressed with the cube's 8 MC vertices (0..7)
static const int TET_IN_CUBE[6][4] = {
    {0,5,1,6},
    {0,1,2,6},
    {0,2,3,6},
    {0,3,7,6},
    {0,7,4,6},
    {0,4,5,6}
};

// edges of a tetrahedron (local indices 0..3)
const int TET_EDGES[6][2] = {
    {0,1}, {1,2}, {2,0}, {0,3}, {1,3}, {2,3}
};

// Triangulation table for a tetrahedron
// Each row: up to 2 triangles (6 edge indices); -1 terminator
const int TET_TRI_TABLE[16][7] = {
    {-1,-1,-1,-1,-1,-1,-1},           // 0  (0000)  no verts inside
    {0,3,2,-1,-1,-1,-1},               // 1  (0001)
    {0,1,4,-1,-1,-1,-1},               // 2  (0010)
    {1,4,2,  2,4,3,-1},                // 3  (0011)
    {1,2,5,-1,-1,-1,-1},               // 4  (0100)
    {0,3,5,  0,5,1,-1},                // 5  (0101)
    {0,2,5,  0,5,4,-1},                // 6  (0110)
    {5,4,3,-1,-1,-1,-1},               // 7  (0111)
    {5,4,3,-1,-1,-1,-1},               // 8  (1000)  complement of 7
    {0,2,5,  0,5,4,-1},                // 9  (1001)  complement of 6
    {0,3,5,  0,5,1,-1},                // 10 (1010)  complement of 5
    {1,2,5,-1,-1,-1,-1},               // 11 (1011)  complement of 4
    {1,4,2,  2,4,3,-1},                // 12 (1100)  complement of 3
    {0,1,4,-1,-1,-1,-1},               // 13 (1101)  complement of 2
    {0,3,2,-1,-1,-1,-1},               // 14 (1110)  complement of 1
    {-1,-1,-1,-1,-1,-1,-1}            // 15 (1111)  all inside
};


int ScalarField3D::polygonize_tetra(const Vec3 p[4], const float val[4],
                                    float iso, std::vector<MCTriangle>& out) const
{
    // 1) case code
    int code = 0;
    if (val[0] < iso) code |= 1;
    if (val[1] < iso) code |= 2;
    if (val[2] < iso) code |= 4;
    if (val[3] < iso) code |= 8;

    const int* row = TET_TRI_TABLE[code];
    if (row[0] == -1) return 0;

    // 2) cache edge intersections
    Vec3 epos[6];
    auto interpEdge = [&](int e)->const Vec3& {
        int a = TET_EDGES[e][0], b = TET_EDGES[e][1];

        // robust interpolation
        const float v1 = val[a], v2 = val[b];
        if (std::fabs(iso - v1) < 1e-6f) { epos[e] = p[a]; return epos[e]; }
        if (std::fabs(iso - v2) < 1e-6f) { epos[e] = p[b]; return epos[e]; }
        if (std::fabs(v1 - v2) < 1e-6f)  { epos[e] = p[a]; return epos[e]; }

        float mu = (iso - v1) / (v2 - v1);
        epos[e] = p[a] + (p[b] - p[a]) * mu;
        return epos[e];
    };

    // 3) Pre-calculate inside reference point once per tetrahedron (OPTIMIZATION)
    Vec3 insideCtr(0.0f, 0.0f, 0.0f);
    int  insideCnt = 0;
    for (int i = 0; i < 4; ++i) {
        if (val[i] < iso) { insideCtr += p[i]; ++insideCnt; }
    }

    // Pre-calculate normalized inside center and determine if orientation check is needed
    bool needOrientationCheck = (insideCnt > 0);
    if (needOrientationCheck) {
        insideCtr /= (float)insideCnt;
    }

    int n = 0;
    for (int i = 0; row[i] != -1; i += 3) {
        MCTriangle tri;
        tri.vertices[0] = interpEdge(row[i  ]);
        tri.vertices[1] = interpEdge(row[i+1]);
        tri.vertices[2] = interpEdge(row[i+2]);

        Vec3 nrm = (tri.vertices[1] - tri.vertices[0]).cross(tri.vertices[2] - tri.vertices[0]);
        float len = nrm.length();
        if (len <= 1e-6f) continue;

        // 4) Optimized orientation check: make normals point *out* of the inside region
        if (needOrientationCheck) {
            // Use first vertex as reference point instead of triangle center for efficiency
            // This is mathematically equivalent for orientation testing
            Vec3 toInside = insideCtr - tri.vertices[0];
            if (nrm.dot(toInside) > 0.0f) {           // pointing toward inside -> flip
                std::swap(tri.vertices[1], tri.vertices[2]);
                nrm = -nrm;
            }
        }

        tri.normal = nrm / len;
        out.push_back(tri);
        ++n;
    }
    return n;
}

int ScalarField3D::polygonize_cell_tetra(const GridCell& cell,
                                         float iso,
                                         std::vector<MCTriangle>& tris) const
{
    int total = 0;
    // For each of the 6 tets
    for (int t=0; t<6; ++t) {
        int i0 = TET_IN_CUBE[t][0];
        int i1 = TET_IN_CUBE[t][1];
        int i2 = TET_IN_CUBE[t][2];
        int i3 = TET_IN_CUBE[t][3];

        Vec3 p[4]   = { cell.vertices[i0], cell.vertices[i1],
                        cell.vertices[i2], cell.vertices[i3] };
        float v[4]  = { cell.values[i0],   cell.values[i1],
                        cell.values[i2],   cell.values[i3] };

        total += polygonize_tetra(p, v, iso, tris);
    }
    return total;
}



} // namespace alice2


