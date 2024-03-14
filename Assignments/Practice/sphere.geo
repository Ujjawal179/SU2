// Gmsh project created on Wed Mar  6 19:31:01 2024
SetFactory("OpenCASCADE");
//+
Point(1) = {-1, 1, 0, 1.0};
//+
Point(2) = {1, 1, 0, 1.0};
//+
Point(3) = {-1, -1, 0, 1.0};
//+
Point(4) = {1, -1, 0, 1.0};
//+
Point(5) = {0, 0, 0, 1.0};
//+
Line(1) = {3, 4};
//+
Line(2) = {4, 2};
//+
Line(3) = {1, 2};
//+
Line(4) = {1, 3};
//+
Point(7) = {-0, 0.4, 0, 1.0};
//+
Point(8) = {0, -0.4, ), 1.0};
//+
Point(8) = {0, -0.4, ), 1.0};
//+
Point(8) = {0, -0.4, 0, 1.0};
//+
Circle(5) = {0, -0, 0, 0.4, 0, 2*Pi};
//+
Curve Loop(1) = {4, 1, 2, -3};
//+
Curve Loop(2) = {5};
//+
Plane Surface(1) = {1, 2};
//+
Physical Curve("a", 6) = {2};
//+
Physical Curve("a", 6) += {3, 1};
