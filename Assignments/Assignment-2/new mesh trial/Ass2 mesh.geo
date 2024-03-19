SetFactory("OpenCASCADE");
//+
Point(1) = {0, 0, 0, 1.0};
Point(2) = {0, 0.5, 0, 1.0};
Point(3) = {1, 0.70, 0, 1.0};
Point(4) = {1, 0, 0, 1.0};
Point(5) = {1, 0.25, 0, 1.0};
Point(6) = {0, 0.05, 0, 1.0};
//+
Line(1) = {2, 6};
Line(2) = {6, 1};
Line(3) = {1, 4};
Line(4) = {4, 5};
Line(5) = {5, 3};
Line(6) = {3, 2};
Line(7) = {6, 5};

//+
Transfinite Curve {1, 5} = 30 Using Progression 1;
Transfinite Curve {6, 7} = 30 Using Progression 1;
Transfinite Curve {2, 4} = 30 Using Progression 1;
Transfinite Curve {3} = 30 Using Progression 1;
//+
Curve Loop(1) = {1, 7, 5, 6};
Plane Surface(1) = {1};
Curve Loop(2) = {2, 3, 4, -7};
Plane Surface(2) = {2};
//+
Transfinite Surface {1};
Transfinite Surface {2};
Recombine Surface {1};
Recombine Surface {2};
//+
Physical Curve("inlet_coflow", 8) = {1};
Physical Curve("inlet", 9) = {2};
Physical Curve("outlet", 10) = {4};
Physical Curve("outlet_coflow", 11) = {5};
Physical Curve("farfield", 12) = {6};
Physical Curve("symmetry", 13) = {3};
