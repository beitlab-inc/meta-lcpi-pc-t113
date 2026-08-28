# Print the BeitlabOS system summary on interactive login shells (serial
# console, LCD getty and SSH). Sourced by /etc/profile.
#
# Turn it off system-wide with BEITLAB_FETCH_ON_LOGIN=0 in
# /etc/beitlab-fetch.conf, or per-user by exporting BEITLAB_FETCH_DONE=1.

case "$-" in
*i*) ;;
*) return 0 2>/dev/null || exit 0 ;;
esac

if [ -z "${BEITLAB_FETCH_DONE:-}" ] && [ -x /usr/bin/beitlab-fetch ]; then
	_blf_enabled=1
	if [ -r /etc/beitlab-fetch.conf ]; then
		_blf_enabled=$(. /etc/beitlab-fetch.conf 2>/dev/null && printf '%s' "${BEITLAB_FETCH_ON_LOGIN:-1}")
	fi
	if [ "$_blf_enabled" = 1 ]; then
		BEITLAB_FETCH_DONE=1
		export BEITLAB_FETCH_DONE
		/usr/bin/beitlab-fetch
	fi
	unset _blf_enabled
fi
