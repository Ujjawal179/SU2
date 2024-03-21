// Gmsh project created on Thu Mar 21 22:35:11 2024
SetFactory("OpenCASCADE");
//+
Point(1) = {0, 0, 0, 1.0};
//+
Circle(1) = {-0, -0, 0, 5, 0, 2*Pi};
//+
Point(3) = {-0.5, 0.05, 0, 1.0};
//+
Point(4) = {0.5, 0.05, 0, 1.0};
//+
Point(5) = {0.5, -0.05, 0, 1.0};
//+
Point(6) = {-0.5, -0.05, 0, 1.0};
//+
Point(7) = {-0.5, 0, 0, 1.0};
//+
Point(8) = {0.5, 0, 0, 1.0};
//+
Line(2) = {3, 4};
//+
Line(3) = {6, 5};
//+
Circle(4) = {6, 7, 3};
//+
Point(9) = {-0.55, 0, 0, 1.0};
//+
Point(10) = {0.55, 0, 0, 1.0};
//+
Circle(4) = {6, 7, 9};
//+
Circle(5) = {9, 7, 3};
//+
Circle(6) = {4, 8, 10};
//+
Circle(7) = {10, 8, 5};
//+
Curve Loop(1) = {1};
//+
Curve Loop(2) = {2, 6, 7, -3, 4, 5};
//+
Plane Surface(1) = {1, 2};
//+
Transfinite Curve {2, 4, 5, 3, 6, 7} = 40 Using Progression 1;
//+
Transfinite Curve {1} = 40 Using Progression 1;
//+
Physical Curve("plate", 8) = {2, 4, 3, 5, 7, 6};
//+
Physical Curve("farfield", 9) = {1};
