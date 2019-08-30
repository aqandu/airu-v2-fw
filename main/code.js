// First, checks if it isn't implemented yet.
if (!String.prototype.format) {
  String.prototype.format = function() {
    var args = arguments;
    return this.replace(/{(\d+)}/g, function(match, number) {
      return typeof args[number] != 'undefined'
        ? args[number]
        : match
      ;
    });
  };
}

var apList = null;
var selectedSSID = "";
var refreshAPInterval = null;
var checkStatusInterval = null;


function stopCheckStatusInterval(){
	if(checkStatusInterval != null){
		clearInterval(checkStatusInterval);
		checkStatusInterval = null;
	}
}

function stopRefreshAPInterval(){
	if(refreshAPInterval != null){
		clearInterval(refreshAPInterval);
		refreshAPInterval = null;
	}
}

function startCheckStatusInterval(){
	checkStatusInterval = setInterval(checkStatus, 950);
}

function startRefreshAPInterval(){
	refreshAPInterval = setInterval(refreshAP, 2800);
}

$(document).ready(function(){


	$("#wifi-status").on("click", ".ape", function() {
		$( "#wifi" ).slideUp( "fast", function() {});
		$( "#connect-details" ).slideDown( "fast", function() {});
	});

	$("#manual_add").on("click", ".ape", function() {
		selectedSSID = $(this).text();
		$( "#ssid-pwd" ).text(selectedSSID);
		$( "#wifi" ).slideUp( "fast", function() {});
		$( "#connect_manual" ).slideDown( "fast", function() {});
		$( "#connect" ).slideUp( "fast", function() {});

		//update wait screen
		$( "#loading" ).show();
		$( "#connect-success" ).hide();
		$( "#connect-fail" ).hide();
		$( "#register" ).hide();
	});

	$("#wifi-list").on("click", ".ape", function() {
		selectedSSID = $(this).text();
		$( "#ssid-pwd" ).text(selectedSSID);
		$( "#wifi" ).slideUp( "fast", function() {});
		$( "#connect_manual" ).slideUp( "fast", function() {});
		$( "#connect" ).slideDown( "fast", function() {});

		//update wait screen
		$( "#loading" ).show();
		$( "#connect-success" ).hide();
		$( "#connect-fail" ).hide();
		$( "#register" ).hide();
	});

	$("#cancel").on("click", function() {
		selectedSSID = "";
		$( "#connect" ).slideUp( "fast", function() {});
		$( "#connect_manual" ).slideUp( "fast", function() {});
		$( "#wifi" ).slideDown( "fast", function() {});
	});

	$("#manual_cancel").on("click", function() {
		selectedSSID = "";
		$( "#connect" ).slideUp( "fast", function() {});
		$( "#connect_manual" ).slideUp( "fast", function() {});
		$( "#wifi" ).slideDown( "fast", function() {});
	});

	$("#join").on("click", function() {
		console.log("join clicked");
		performConnect();
	});

	$("#manual_join").on("click", function() {
		performConnect($(this).data('connect'));
	});

	$("#reg_btn").on("click", function() {
		performRegistration();
	})

	$("#ok-details").on("click", function() {
		$( "#connect-details" ).slideUp( "fast", function() {});
		$( "#wifi" ).slideDown( "fast", function() {});

	});

	$("#ok-credits").on("click", function() {
		$( "#credits" ).slideUp( "fast", function() {});
		$( "#app" ).slideDown( "fast", function() {});

	});

	$("#acredits").on("click", function(event) {
		event.preventDefault();
		$( "#app" ).slideUp( "fast", function() {});
		$( "#credits" ).slideDown( "fast", function() {});
	});

	$("#ok-connect").on("click", function() {
		console.log("ok-connect clicked");
		$( "#connect-wait" ).slideUp( "fast", function() {});
		$( "#wifi" ).slideDown( "fast", function() {});
	});

	$("#disconnect").on("click", function() {
		$( "#connect-details-wrap" ).addClass('blur');
		$( "#diag-disconnect" ).slideDown( "fast", function() {});
	});

	$("#no-disconnect").on("click", function() {
		$( "#diag-disconnect" ).slideUp( "fast", function() {});
		$( "#connect-details-wrap" ).removeClass('blur');
	});

	$("#yes-disconnect").on("click", function() {

		stopCheckStatusInterval();
		selectedSSID = "";

		$( "#diag-disconnect" ).slideUp( "fast", function() {});
		$( "#connect-details-wrap" ).removeClass('blur');

		$.ajax({
			url: '/connect.json',
			dataType: 'json',
			method: 'DELETE',
			cache: false,
			data: { 'timestamp': Date.now()}
		});

		startCheckStatusInterval();

		$( "#connect-details" ).slideUp( "fast", function() {});
		$( "#wifi" ).slideDown( "fast", function() {})
	});

	//first time the page loads: attempt get the connection status and start the wifi scan
	refreshAP();
	startCheckStatusInterval();
	startRefreshAPInterval();
	console.log("Calling 'lastRegistered' inside doc.ready");
	lastRegistered();

});




function performConnect(conntype){

	//stop the status refresh. This prevents a race condition where a status
	//request would be refreshed with wrong ip info from a previous connection
	//and the request would automatically shows as succesful.
	stopCheckStatusInterval();

	//stop refreshing wifi list
	stopRefreshAPInterval();

	var pwd;
	if (conntype == 'manual') {
		//Grab the manual SSID and PWD
		selectedSSID=$('#manual_ssid').val();
		pwd = $("#manual_pwd").val();
	}else{
		pwd = $("#pwd").val();
	}
	//reset connection
	$( "#loading" ).show();
	$( "#connect-success" ).hide();
	$( "#connect-fail" ).hide();
	$( "#register" ).hide();

	$( "#ok-connect" ).prop("disabled",true);
	$( "#ssid-wait" ).text(selectedSSID);
	$( "#connect" ).slideUp( "fast", function() {});
	$( "#connect_manual" ).slideUp( "fast", function() {});
	$( "#connect-wait" ).slideDown( "fast", function() {});

	// selectedSSID = "Tom\'s iPhone";
	// pwd = "nitroT14";
	// selectedSSID = "SuckMyGig";
	// pwd = "yRrMZ4nkhYBo";
	var hdrs = JSON.stringify({'X-Custom-ssid': selectedSSID, 'X-Custom-pwd': pwd})
	console.log("HEADERS: " + hdrs);
	$.ajax({
		url: '/connect.json',
		dataType: 'json',
		method: 'POST',
		cache: false,
		headers: {'X-Custom-ssid': selectedSSID, 'X-Custom-pwd': pwd},
		// data: JSON.stringify({'timestamp': Date.now()}),
		success: function(msg){
			console.log( "Data Saved: " + msg );
	  	},
		error: function(XMLHttpRequest, textStatus, errorThrown) {
			console.log("POST Error. Headers: " + hdrs + "Error: " + errorThrown);
		}
	});


	//now we can re-set the intervals regardless of result
	startCheckStatusInterval();
	startRefreshAPInterval();

}

function performRegistration(){

	var name = $( "#reg_name" ).val();
	var email = $( "#reg_email" ).val();
	var hidden = document.getElementById("hide_gps").checked;

	name = CleanName(name);
	if(name.length === 0) {
		return;
	}
	if(validate(email)){
		$.ajax({
			url: '/register.json',
			dataType: 'json',
			method: 'POST',
			cache: false,
			headers: {'X-Custom-name': name, 'X-Custom-email': email, 'X-Custom-hidden': hidden },
			data: {'timestamp': Date.now()}
		});
		console.log("Calling 'lastRegistered' inside 'performRegistration'");
		lastRegistered();
	}
}

function lastRegistered(){
	// console.log("Last registered function...");
	$.getJSON( "/register.json", function( data ) {
		var h1 = "";
		var h2 = "";
		if(data['name'].length > 0){
			// console.log("got data");
			h1 += '<h4>This device was last registered to {0}</h4>'.format(data['name']);
		}
		else {
			// console.log("couldn't get data");
			h1 += '<h4>This device is currently unregistered</h4>';
		}
		if(data['macAddress'].length === 0) {
			h2 += '<h4>Could not retreive mac address</h4>';
		}
		else {
			h2 += '<h4>MAC Address: {0}</h4>'.format(data['macAddress']);
		}
		$( "#last-reg" ).html(h1);
		$( "#mac-addr" ).html(h2);
	});
}

function validateName(name) {
	var res = name.split(" ");
	// check for at least 2 names
	if (res.length < 2) {
		return false;
	}
	// check that last name is valid
	if (res[1].length < 2) {
		return false;
	}
	else {
		return true;
	}
}

function CleanName() {
  var $result = $("#name_result");
  var name = $("#reg_name").val();
  $result.text("");

  if (!validateName(name)) {

    $result.text("Please include both first and last name.");
    $result.css("color", "red");

    return "";
  }
  else {
  	var res = name.split(' ');
  	var newarr = [];
  	for(var x = 0; x < res.length; x++) {
  		newarr.push(res[x].charAt(0).toUpperCase() + res[x].slice(1));
  	}
    return newarr.join(' ');
  }
}

function validateEmail(email) {
  var re = /^(([^<>()[\]\\.,;:\s@\"]+(\.[^<>()[\]\\.,;:\s@\"]+)*)|(\".+\"))@((\[[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\])|(([a-zA-Z\-0-9]+\.)+[a-zA-Z]{2,}))$/;
  return re.test(email);
}

function validate() {
  var $result = $("#email_result");
  var email = $("#reg_email").val();
  $result.text("");

  if (!validateEmail(email)) {

    $result.text(email + " is not a valid email address.");
    $result.css("color", "red");
  } else {
    return true;
  }
  return false;
}


function rssiToIcon(rssi){
	if(rssi >= -60){
		return 'w0';
	}
	else if(rssi >= -67){
		return 'w1';
	}
	else if(rssi >= -75){
		return 'w2';
	}
	else{
		return 'w3';
	}
}


function refreshAP(){
	$.getJSON( "/ap.json", function( data ) {
		if(data.length > 0){
			//sort by signal strength
			data.sort(function (a, b) {
				var x = a["rssi"]; var y = b["rssi"];
				return ((x < y) ? 1 : ((x > y) ? -1 : 0));
			});
			apList = data;
			refreshAPHTML(apList);
		}
	});
}

function refreshAPHTML(data){
	var h = "";
	data.forEach(function(e, idx, array) {
		h += '<div class="ape{0}"><div class="{1}"><div class="{2}">{3}</div></div></div>'.format(idx === array.length - 1?'':' brdb', rssiToIcon(e.rssi), e.auth==0?'':'pw',e.ssid);
		h += "\n";
	});

	$( "#wifi-list" ).html(h)
	// console.log("calling lastRegistered from refreshAPHTML");
	lastRegistered();
}

function togglepw(div_id){
	var x = document.getElementById(div_id);
	if (x.type === "password") {
		x.type = "text";
	} else {
		x.type = "password";
	}
}

function checkStatus(){
	$.getJSON( "/status.json", function( data ) {
		if(data.hasOwnProperty('ssid') && data['ssid'] != ""){
			if(data["ssid"] === selectedSSID){
				//that's a connection attempt
				if(data["urc"] === 0){
					//got connection
					$("#connected-to span").text(data["ssid"]);
					$("#connect-details h1").text(data["ssid"]);
					$("#ip").text(data["ip"]);
					$("#netmask").text(data["netmask"]);
					$("#gw").text(data["gw"]);
					$("#wifi-status").slideDown( "fast", function() {});

					//unlock the wait screen if needed
					$( "#ok-connect" ).prop("disabled",false);

					//update wait screen
					$( "#loading" ).hide();
					// $( "#connect-success" ).show();
					$("#register").show();
					$( "#connect-fail" ).hide();
				}
				else if(data["urc"] === 1){
					//failed attempt
					$("#connected-to span").text('');
					$("#connect-details h1").text('');
					$("#ip").text('0.0.0.0');
					$("#netmask").text('0.0.0.0');
					$("#gw").text('0.0.0.0');

					//don't show any connection
					$("#wifi-status").slideUp( "fast", function() {});

					//unlock the wait screen
					$( "#ok-connect" ).prop("disabled",false);

					//update wait screen
					$( "#loading" ).hide();
					$( "#connect-fail" ).show();
					$( "#connect-success" ).hide();
					$( "#register" ).hide();
				}
			}
			else if(data.hasOwnProperty('urc') && data['urc'] === 0){
				//ESP32 is already connected to a wifi without having the user do anything
				if( !($("#wifi-status").is(":visible")) ){
					$("#connected-to span").text(data["ssid"]);
					$("#connect-details h1").text(data["ssid"]);
					$("#ip").text(data["ip"]);
					$("#netmask").text(data["netmask"]);
					$("#gw").text(data["gw"]);
					$("#wifi-status").slideDown( "fast", function() {});
				}
			}
		}
		else if(data.hasOwnProperty('urc') && data['urc'] === 2){
			//that's a manual disconnect
			if($("#wifi-status").is(":visible")){
				$("#wifi-status").slideUp( "fast", function() {});
			}
		}
	})
	.fail(function() {
		// this is a new comment!
	});
}
