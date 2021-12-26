#pragma once

namespace KNet
{
#ifdef _DEBUG
#define KN_CHECK_RESULT(f,t)					\
{												\
	if (f == t)									\
	{											\
		std::string ERR("[KNet][FATAL]: (");	\
		ERR += std::to_string(GetLastError());	\
		ERR += std::string(") in ");			\
		ERR += std::string(__FILE__);			\
		ERR += std::string(" at line ");		\
		ERR += std::to_string(__LINE__);		\
		ERR += std::string("\n");				\
		printf("%s", ERR.c_str());				\
	}											\
}

	bool DF(bool flag, std::string F, std::string L)
	{
		if (flag)
		{
			std::string ERR("[KNet][Code ");
				ERR += std::to_string(GetLastError());
				ERR += std::string("]: ");
				ERR += F;
				ERR += std::string(" (line ");
				ERR += L;
				ERR += std::string(")\n");
				printf("%s", ERR.c_str());
			return true;
		}
		return false;
	}

#define KN_CHECK_RESULT2(f,t) \
	DF((f == t), std::string(__FILE__), std::to_string(__LINE__))

#else
#define KN_CHECK_RESULT(f,t)	\
	f;

#define KN_CHECK_RESULT2(f,t)	\
	f == t

#endif
}