var dtmfin = require('./build/Release/dtmfin');

var devices = dtmfin.list();


console.log("Found:",devices);

if (devices.length > 0) {
	try	{
	  var info = dtmfin.open(-1, function(code){
	    console.log("lklaskjd", code, arguments);
	  });
	  console.log(info);
	  process.stdin.resume();
	} catch (error) {
		console.error("ERROR: ",error);
	}
  //dtmfin.close();
}

