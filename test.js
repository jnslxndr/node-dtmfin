var dtmfin = require('./build/Release/dtmfin');

var devices = dtmfin.list();


console.log("Found:",devices);

if (devices.length > 0) {
  var info = dtmfin.open(0, function(code){
    console.log("lklaskjd", code);
  });
  console.log(info);
  process.stdin.resume();
  // dtmfin.close();
}

