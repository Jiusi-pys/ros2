#include <stddef.h>
#include <string.h>

static char g_textdomain[256] = "messages";
static char g_bindtextdomain[512] = "";
static char g_codeset[64] = "";

static char *copy_or_current(char *storage, size_t storage_size, const char *value)
{
  if (value == NULL) {
    return storage;
  }
  if (storage_size == 0) {
    return storage;
  }
  strncpy(storage, value, storage_size - 1);
  storage[storage_size - 1] = '\0';
  return storage;
}

char *libintl_gettext(const char *msgid)
{
  return (char *)(msgid != NULL ? msgid : "");
}

char *libintl_dgettext(const char *domainname, const char *msgid)
{
  (void)domainname;
  return libintl_gettext(msgid);
}

char *libintl_dcgettext(const char *domainname, const char *msgid, int category)
{
  (void)domainname;
  (void)category;
  return libintl_gettext(msgid);
}

char *libintl_textdomain(const char *domainname)
{
  return copy_or_current(g_textdomain, sizeof(g_textdomain), domainname);
}

char *libintl_bindtextdomain(const char *domainname, const char *dirname)
{
  (void)domainname;
  return copy_or_current(g_bindtextdomain, sizeof(g_bindtextdomain), dirname);
}

char *libintl_bind_textdomain_codeset(const char *domainname, const char *codeset)
{
  (void)domainname;
  return copy_or_current(g_codeset, sizeof(g_codeset), codeset);
}

char *gettext(const char *msgid)
{
  return libintl_gettext(msgid);
}

char *dgettext(const char *domainname, const char *msgid)
{
  return libintl_dgettext(domainname, msgid);
}

char *dcgettext(const char *domainname, const char *msgid, int category)
{
  return libintl_dcgettext(domainname, msgid, category);
}

char *textdomain(const char *domainname)
{
  return libintl_textdomain(domainname);
}

char *bindtextdomain(const char *domainname, const char *dirname)
{
  return libintl_bindtextdomain(domainname, dirname);
}

char *bind_textdomain_codeset(const char *domainname, const char *codeset)
{
  return libintl_bind_textdomain_codeset(domainname, codeset);
}
