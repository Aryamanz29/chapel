use UnitTest;
var sep ="=="*40;
var x = [1,2,3];
var y = (1,2);
try {
  UnitTest.assertLessThan(x,y);
}
catch e {
  writeln(e);
  writeln(sep);
}
var x1 = (1,2);
var y1 = [1,2];
try {
  UnitTest.assertLessThan(x1,y1);
}
catch e {
  writeln(e);
  writeln(sep);
}