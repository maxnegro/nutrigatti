$( document ).ready(function() {

  function periodicUpdate() {
    $.ajax({
      url: '/status',
      success: function(data) {
        $.each(data, function(key, value, data) {
          if (key === 'lastFed') {
            var a = moment(value);
            $('#lastFed').text(a.tz('Europe/Rome').format('LLL'));
            $('#lastFedAgo').text(a.tz('Europe/Rome').fromNow());
          } else if (key === 'currentTime') {
            var a = moment(value);
            $('#currentTime').text(a.tz('Europe/Rome').format('LLL'));
          } else if (key === 'schedule') {
            // array[0..4] of array[hr, min, revolutions]
            if (Array.isArray(value) && (value.length > 0)) {
              outHtml  = '<table class="table table-striped">';
              outHtml += '<tr><th>Ora</th><th>Scatti</th></tr>';
              for (i=0; i < value.length; i++) {
                var a = moment();
                a.hour(value[i][0]);
                a.minute(value[i][1]);
                outHtml += "<tr><td>" + a.format('LT') + "</td><td>" + value[i][2] + "</td></tr>"
              }
              outHtml += "</table>";
              $('#schedule').html(outHtml);
            }
          }

        });
      },
      complete: function() {
        setTimeout(periodicUpdate, 5000);
      }
    });
  }
  moment.locale('it');
  setTimeout(periodicUpdate, 500);
});
