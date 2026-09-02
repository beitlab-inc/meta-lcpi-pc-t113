# Optionally print the BeitlabOS system summary on interactive login shells
# (serial console, LCD getty and SSH). Sourced by /etc/profile.
#
# Disabled unless BEITLAB_FETCH_ON_LOGIN=1 is set in /etc/beitlab-fetch.conf;
# otherwise the summary only appears when `beitlab-fetch` is run by hand.
# A user can also suppress it by exporting BEITLAB_FETCH_DONE=1.

case "$-" in
*i*) ;;
*) return 0 2>/dev/null || exit 0 ;;
esac

if [ -z "${BEITLAB_FETCH_DONE:-}" ] && [ -x /usr/bin/beitlab-fetch ]; then
	_blf_enabled=0
	if [ -r /etc/beitlab-fetch.conf ]; then
		_blf_enabled=$(. /etc/beitlab-fetch.conf 2>/dev/null && printf '%s' "${BEITLAB_FETCH_ON_LOGIN:-0}")
	fi
	if [ "$_blf_enabled" = 1 ]; then
		BEITLAB_FETCH_DONE=1
		export BEITLAB_FETCH_DONE
		/usr/bin/beitlab-fetch
	fi
	unset _blf_enabled
fi
