module M {
  proc foo() {
    writeln("In foo");
  }

  proc boo() {
    writeln("In boo");
  }
}

module M2 {
  use M only ;

  proc main() {
    foo();
  }
}
