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
          }

        });
      },
      complete: function() {
        setTimeout(periodicUpdate, 1000);
      }
    });
  }
  moment.locale('it');
  setTimeout(periodicUpdate, 500);
});
