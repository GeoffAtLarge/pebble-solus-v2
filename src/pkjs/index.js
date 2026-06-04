
var xhrRequest = function (url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    callback(this.responseText);
  };
  xhr.open(type, url);
  xhr.send();
};

function requestDaylightData(pos) {
  // Construct URL

  var url = "http://api.sunrise-sunset.org/json?date=today&formatted=0&lat=" +
      pos.coords.latitude + "&lng=" + pos.coords.longitude;  
  // Send request to OpenWeatherMap
    console.log("Request: "+url);
  xhrRequest(url, 'GET', 
    function(responseText) {
      // responseText contains a JSON object with weather info
      var json = JSON.parse(responseText);
      console.log(responseText);

      var date = new Date();
      date.setHours(0);
      date.setMinutes(0);
      date.setSeconds(0);
      
      var rise = new Date(json.results.sunrise);
      var set = new Date(json.results.sunset); 
      
      console.log("Now is " + date);       
      console.log("Sunrise  " + rise);
      console.log("Sunset  " + set);  
   
      var rise_diff = parseInt((rise.getTime() - date.getTime()) / 1000);
      var set_diff = parseInt((set.getTime() - date.getTime()) / 1000);

      console.log("Sunrise  " + rise_diff);
      console.log("Sunset  " + set_diff);      
      

      // Assemble dictionary using our keys
      var dictionary = {
        "KEY_SUNRISE": rise_diff,
        "KEY_SUNSET": set_diff 
      };

      // Send to Pebble
      Pebble.sendAppMessage(dictionary,
        function(e) {
          console.log("Daylight info sent to Pebble successfully! ");
        },
        function(e) {
          console.log("Error sending Daylight info to Pebble! ");
        }
      );
    }      
  );
}

function locationError(err) {
  console.log("Error requesting location!");
}

function getLocation(successFunc) {
  
  navigator.geolocation.getCurrentPosition(
    successFunc,
    locationError,
    {timeout: 15000, maximumAge: 60000}
  );
}


  Pebble.addEventListener('appmessage',
    function(e) {
      console.log("AppMessage received!");

        getLocation(requestDaylightData);
   
    }                     
  );

