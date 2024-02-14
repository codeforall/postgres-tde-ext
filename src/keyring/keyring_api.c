
#include "keyring/keyring_api.h"
#include "keyring/keyring_file.h"
#include "keyring/keyring_vault.h"
#include "keyring/keyring_config.h"

#include "postgres.h"
#include "access/xlog.h"
#include "storage/shmem.h"
#include "nodes/pg_list.h"
#include "utils/memutils.h"

#include <assert.h>
#include <openssl/rand.h>

typedef struct KeyProviders
{
	TDEKeyringRoutine* routine;
	ProviderType type;
} KeyProviders;

List *registeredKeyProviders = NIL;
static KeyProviders* findKeyProvider(ProviderType type);

static KeyProviders*
findKeyProvider(ProviderType type)
{
	ListCell *lc;
	foreach(lc, registeredKeyProviders)
	{
		KeyProviders* kp = (KeyProviders*)lfirst(lc);
		if(kp->type == type)
		{
			return kp;
		}
	}
	return NULL;
}
bool
RegisterKeyProvider(const TDEKeyringRoutine* routine, ProviderType type)
{
	MemoryContext oldcontext;
	KeyProviders* kp;

	Assert(routine != NULL);
	Assert(routine->keyring_get_key != NULL);
	Assert(routine->keyring_store_key != NULL);

	kp = findKeyProvider(type);
	if (kp)
	{
		ereport(ERROR,
				(errmsg("Key provider of type %d already registered", type)));
		return false;
	}
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);
	kp = palloc(sizeof(KeyProviders));
	kp->routine = (TDEKeyringRoutine*)routine;
	kp->type = type;
	registeredKeyProviders = lappend(registeredKeyProviders, kp);
	MemoryContextSwitchTo(oldcontext);
	return true;
}

keyInfo*
KeyringGetKey(GenericKeyring* keyring, const char* key_name, bool throw_error, KeyringReturnCodes *returnCode)
{
	KeyProviders* kp = findKeyProvider(keyring->type);
	if(kp == NULL)
	{
		ereport(throw_error?ERROR:WARNING,
				(errmsg("Key provider of type %d not registered", keyring->type)));
		*returnCode = KEYRING_CODE_INVALID_PROVIDER;
		return NULL;
	}
	return kp->routine->keyring_get_key(keyring, key_name, throw_error, returnCode);
}

KeyringReturnCodes
KeyringStoreKey(GenericKeyring* keyring, keyInfo *key, bool throw_error)
{
	KeyProviders* kp = findKeyProvider(keyring->type);
	if(kp == NULL)
	{
		ereport(throw_error?ERROR:WARNING,
				(errmsg("Key provider of type %d not registered", keyring->type)));
		return KEYRING_CODE_INVALID_PROVIDER;
	}
	return kp->routine->keyring_store_key(keyring, key, throw_error);
}


keyName keyringConstructKeyName(const char* internalName, unsigned version)
{
	keyName name;
	if(keyringKeyPrefix != NULL && strlen(keyringKeyPrefix) > 0)
	{
		snprintf(name.name, sizeof(name.name), "%s-%s-%u", keyringKeyPrefix, internalName, version);
	} else 
	{
		snprintf(name.name, sizeof(name.name), "%s-%u", internalName, version);
	}
	return name;
}


keyInfo*
keyringGenerateNewKey(const char* key_name, unsigned key_len)
{
	keyInfo *key;
	Assert(key_len <= 32);
	key = palloc(sizeof(keyInfo));
	key->data.len = key_len;
	if (!RAND_bytes(key->data.data, key_len)) 
	{
		pfree(key);
		return NULL; // openssl error
	}
	strncpy(key->name.name, key_name, sizeof(key->name.name));
	return key;
}

keyInfo*
keyringGenerateNewKeyAndStore(GenericKeyring* keyring, const char* key_name, unsigned key_len, bool throw_error)
{
	keyInfo *key = keyringGenerateNewKey(key_name, key_len);
	if(key == NULL)
	{
		ereport(throw_error?ERROR:WARNING,
				(errmsg("Failed to generate key")));
		return NULL;
	}
	if(KeyringStoreKey(keyring, key, throw_error) != KEYRING_CODE_SUCCESS)
	{
		pfree(key);
		return NULL;
	}
	return key;
}
