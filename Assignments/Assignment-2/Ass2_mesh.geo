SetFactory("OpenCASCADE");
//+
Point(1) = {0, 0, 0, 1.0};
Point(2) = {0, 1, 0, 1.0};
Point(3) = {1, 1, 0, 1.0};
Point(4) = {1, 0, 0, 1.0};
Point(5) = {1, 0.15, 0, 1.0};
Point(6) = {0, 0.1, 0, 1.0};
//+
Line(1) = {2, 6};
Line(2) = {6, 1};
Line(3) = {1, 4};
Line(4) = {4, 5};
Line(5) = {5, 3};
Line(6) = {3, 2};
Line(7) = {6, 5};

//+
Curve Loop(1) = {1, 7, 5, 6};
Plane Surface(1) = {1};
Curve Loop(2) = {2, 3, 4, -7};
Plane Surface(2) = {2};
//+
Transfinite Curve {2, 4} = 20 Using Progression 1;
//+
Transfinite Curve {1, 5} = 50 Using Progression 1;
//+
Transfinite Curve {3, 7, 6} = 160 Using Progression 1;
//+
Physical Curve("inlet", 8) = {2};
//+
Physical Curve("outlet", 9) = {5, 4};
//+
Physical Curve("symmetry", 10) = {1, 6};
//+
Physical Curve("wall", 11) = {3};
//+
Physical Surface("fluid", 12) = {1, 2};
//+
Transfinite Surface {1};
//+
Transfinite Surface {2};
//+
Recombine Surface {1, 2};