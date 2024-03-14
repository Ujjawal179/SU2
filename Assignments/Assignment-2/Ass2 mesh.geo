SetFactory("OpenCASCADE");
//+
Point(1) = {0, 0, 0, 1.0};
//+
Point(2) = {0, 1, 0, 1.0};
//+
Point(3) = {1, 1, 0, 1.0};
//+
Point(4) = {1, -0, 0, 1.0};
//+
Point(5) = {1, 0.5, 0, 1.0};
//+
Point(6) = {0, 0.1, 0, 1.0};
//+
Line(1) = {2, 6};
//+
Line(2) = {6, 1};
//+
Line(3) = {1, 4};
//+
Line(4) = {4, 5};
//+
Line(5) = {5, 3};
//+
Line(6) = {3, 2};
//+
Line(7) = {6, 5};

//+
Transfinite Curve {1, 5} = 10 Using Progression 1;
//+
Transfinite Curve {6, 7} = 10 Using Progression 1;
//+
Transfinite Curve {2, 4} = 10 Using Progression 1;
//+
Transfinite Curve {3} = 10 Using Progression 1;
//+
Curve Loop(1) = {1, 7, 5, 6};
//+
Plane Surface(1) = {1};
//+
Curve Loop(2) = {2, 3, 4, -7};
//+
Plane Surface(2) = {2};
//+
Transfinite Surface {1};
//+
Transfinite Surface {2};
//+
Recombine Surface {1};
//+
Recombine Surface {2};
