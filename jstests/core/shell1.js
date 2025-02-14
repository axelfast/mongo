x = 1;

shellHelper("show", "tables;");
shellHelper("show", "tables");
shellHelper("show", "tables ;");

// test slaveOk levels
assert(!db.getSlaveOk() && !db.test.getSlaveOk() && !db.getMonger().getSlaveOk(), "slaveOk 1");
db.getMonger().setSlaveOk();
assert(db.getSlaveOk() && db.test.getSlaveOk() && db.getMonger().getSlaveOk(), "slaveOk 2");
db.setSlaveOk(false);
assert(!db.getSlaveOk() && !db.test.getSlaveOk() && db.getMonger().getSlaveOk(), "slaveOk 3");
db.test.setSlaveOk(true);
assert(!db.getSlaveOk() && db.test.getSlaveOk() && db.getMonger().getSlaveOk(), "slaveOk 4");
