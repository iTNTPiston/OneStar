#include <iostream>
#include "Util.h"
#include "Calculator.h"
#include "Const.h"
#include "XoroshiroState.h"
#include "Data.h"

// 検索条件設定
static PokemonData l_Pokemon[3];

static int g_Rerolls;
static int g_FixedIndex;
static bool g_isEnableThird;

// 絞り込み条件設定

// V確定用参照
const int* g_IvsRef[30] = {
	&(l_Pokemon[0].ivs[1]), &(l_Pokemon[0].ivs[2]), &(l_Pokemon[0].ivs[3]), &(l_Pokemon[0].ivs[4]), &(l_Pokemon[0].ivs[5]),
	&(l_Pokemon[0].ivs[0]), &(l_Pokemon[0].ivs[2]), &(l_Pokemon[0].ivs[3]), &(l_Pokemon[0].ivs[4]), &(l_Pokemon[0].ivs[5]),
	&(l_Pokemon[0].ivs[0]), &(l_Pokemon[0].ivs[1]), &(l_Pokemon[0].ivs[3]), &(l_Pokemon[0].ivs[4]), &(l_Pokemon[0].ivs[5]),
	&(l_Pokemon[0].ivs[0]), &(l_Pokemon[0].ivs[1]), &(l_Pokemon[0].ivs[2]), &(l_Pokemon[0].ivs[4]), &(l_Pokemon[0].ivs[5]),
	&(l_Pokemon[0].ivs[0]), &(l_Pokemon[0].ivs[1]), &(l_Pokemon[0].ivs[2]), &(l_Pokemon[0].ivs[3]), &(l_Pokemon[0].ivs[5]),
	&(l_Pokemon[0].ivs[0]), &(l_Pokemon[0].ivs[1]), &(l_Pokemon[0].ivs[2]), &(l_Pokemon[0].ivs[3]), &(l_Pokemon[0].ivs[4]),
};

#define LENGTH_BASE (56)

// 夢特性なし→特性指定ありの場合AbilityBitが有効
// 夢特性あり→特性2の時のみAbilityBitが有効(1か3なので奇数)
inline bool IsEnableAbilityBit()
{
	return (l_Pokemon[0].ability == 1) || (l_Pokemon[0].abilityFlag == 3 && l_Pokemon[0].ability >= 0);
}

void Set12Condition(int index, int iv0, int iv1, int iv2, int iv3, int iv4, int iv5, int ability, int nature, int characteristic, bool isNoGender, int abilityFlag, int flawlessIvs)
{
	if(index < 0 || index >= 3)
	{
		return;
	}
	if(index != 2)
	{
		g_isEnableThird = false;
	}
	else
	{
		g_isEnableThird = true; // 3匹目は必ず最後に登録
	}

	l_Pokemon[index].ivs[0] = iv0;
	l_Pokemon[index].ivs[1] = iv1;
	l_Pokemon[index].ivs[2] = iv2;
	l_Pokemon[index].ivs[3] = iv3;
	l_Pokemon[index].ivs[4] = iv4;
	l_Pokemon[index].ivs[5] = iv5;
	l_Pokemon[index].ability = ability;
	l_Pokemon[index].nature = nature;
	l_Pokemon[index].characteristic = characteristic;
	l_Pokemon[index].isNoGender = isNoGender;
	l_Pokemon[index].abilityFlag = abilityFlag;
	l_Pokemon[index].flawlessIvs = flawlessIvs;

	// 計算用
	if(index == 0)
	{
		for(int i = 0; i < 6; ++i)
		{
			if(l_Pokemon[index].ivs[i] == 31)
			{
				g_FixedIndex = i;
				break;
			}
		}
	}
}

void Prepare(int rerolls)
{
	const int length = (IsEnableAbilityBit() ? LENGTH_BASE + 1 : LENGTH_BASE);

	g_Rerolls = rerolls;

	// 使用する行列値をセット
	// 使用する定数ベクトルをセット
	
	g_ConstantTermVector = 0;

	// r[3+rerolls]をV箇所、r[4+rerolls]からr[8+rerolls]を個体値として使う

	// 変換行列を計算
	InitializeTransformationMatrix(false); // r[1]が得られる変換行列がセットされる
	for(int i = 0; i <= rerolls + 1; ++i)
	{
		ProceedTransformationMatrix(); // r[2 + i]が得られる
	}

	int bit = 0;
	for (int i = 0; i < 6; ++i, ++bit)
	{
		int index = 61 + (i / 3) * 64 + (i % 3);
		g_InputMatrix[bit] = GetMatrixMultiplier(index);
		if(GetMatrixConst(index) != 0)
		{
			g_ConstantTermVector |= (1ull << (length - 1 - bit));
		}
	}
	for (int a = 0; a < 5; ++a)
	{
		ProceedTransformationMatrix();
		for(int i = 0; i < 10; ++i, ++bit)
		{
			int index = 59 + (i / 5) * 64 + (i % 5);
			g_InputMatrix[bit] = GetMatrixMultiplier(index);
			if(GetMatrixConst(index) != 0)
			{
				g_ConstantTermVector |= (1ull << (length - 1 - bit));
			}
		}
	}
	// Abilityは2つを圧縮 r[9+rerolls]
	if(IsEnableAbilityBit())
	{
		ProceedTransformationMatrix();

		g_InputMatrix[LENGTH_BASE] = GetMatrixMultiplier(63) ^ GetMatrixMultiplier(127);
		if((GetMatrixConst(63) ^ GetMatrixConst(127)) != 0)
		{
			g_ConstantTermVector |= 1;
		}
	}

	// 行基本変形で求める
	CalculateInverseMatrix(length);

	// 事前データを計算
	CalculateCoefficientData(length);
}

_u64 Search(_u64 ivs)
{
	const int length = (IsEnableAbilityBit() ? LENGTH_BASE + 1 : LENGTH_BASE);

	XoroshiroState xoroshiro;
	XoroshiroState oshiroTemp;
	XoroshiroState oshiroTemp2;

	_u64 target = (IsEnableAbilityBit() ? (l_Pokemon[0].ability & 1) : 0);
	int bitOffset = (IsEnableAbilityBit() ? 1 : 0);

	// 上位3bit = V箇所決定
	target |= (ivs & 0xE000000ul) << (28 + bitOffset); // fixedIndex0

	// 下位25bit = 個体値
	target |= (ivs & 0x1F00000ul) << (25 + bitOffset); // iv0_0
	target |= (ivs &   0xF8000ul) << (20 + bitOffset); // iv1_0
	target |= (ivs &    0x7C00ul) << (15 + bitOffset); // iv2_0
	target |= (ivs &     0x3E0ul) << (10 + bitOffset); // iv3_0
	target |= (ivs &      0x1Ful) << ( 5 + bitOffset); // iv4_0

	// 隠された値を推定
	target |= ((8ul + g_FixedIndex - ((ivs & 0xE000000ul) >> 25)) & 7) << (50 + bitOffset);

	target |= ((32ul + *g_IvsRef[g_FixedIndex * 5    ] - ((ivs & 0x1F00000ul) >> 20)) & 0x1F) << (40 + bitOffset);
	target |= ((32ul + *g_IvsRef[g_FixedIndex * 5 + 1] - ((ivs &   0xF8000ul) >> 15)) & 0x1F) << (30 + bitOffset);
	target |= ((32ul + *g_IvsRef[g_FixedIndex * 5 + 2] - ((ivs &    0x7C00ul) >> 10)) & 0x1F) << (20 + bitOffset);
	target |= ((32ul + *g_IvsRef[g_FixedIndex * 5 + 3] - ((ivs &     0x3E0ul) >> 5)) & 0x1F) << (10 + bitOffset);
	target |= ((32ul + *g_IvsRef[g_FixedIndex * 5 + 4] -  (ivs &      0x1Ful)) & 0x1F) << bitOffset;

	// targetベクトル入力完了

	target ^= g_ConstantTermVector;

	// 56~57bit側の計算結果キャッシュ
	_u64 processedTarget = 0;
	int offset = 0;
	for (int i = 0; i < length; ++i)
	{
		while (g_FreeBit[i + offset] > 0)
		{
			++offset;
		}
		processedTarget |= (GetSignature(g_AnswerFlag[i] & target) << (63 - (i + offset)));
	}

	// 下位7bitを決める
	_u64 max = ((1 << (64 - length)) - 1);
	for (_u64 search = 0; search <= max; ++search)
	{
		_u64 seed = (processedTarget ^ g_CoefficientData[search]) | g_SearchPattern[search];
		
		// ここから絞り込み
		{
			xoroshiro.SetSeed(seed);
			xoroshiro.Next(); // EC
			xoroshiro.Next(); // OTID
			xoroshiro.Next(); // PID

			// V箇所
			int offset = -1;
			int fixedIndex = 0;
			do {
				fixedIndex = xoroshiro.Next(7); // V箇所
				++offset;
			} while (fixedIndex >= 6);

			// reroll回数
			if (offset != g_Rerolls)
			{
				continue;
			}

			xoroshiro.Next(); // 個体値1
			xoroshiro.Next(); // 個体値2
			xoroshiro.Next(); // 個体値3
			xoroshiro.Next(); // 個体値4
			xoroshiro.Next(); // 個体値5

			// イベントレイドの夢特性強制モード
			bool isPassed = false;
			if(l_Pokemon[0].abilityFlag == 2 && l_Pokemon[0].ability == 2)
			{
				oshiroTemp2.Copy(&xoroshiro);

				// 特性スキップ

				// 性別値
				if(!l_Pokemon[0].isNoGender)
				{
					int gender = 0;
					do {
						gender = xoroshiro.Next(0xFF); // 性別値
					} while(gender >= 253);
				}

				int nature = 0;
				do {
					nature = xoroshiro.Next(0x1F); // 性格
				} while(nature >= 25);

				if(nature == l_Pokemon[0].nature)
				{
					isPassed = true;
				}

				xoroshiro.Copy(&oshiroTemp2);
			}
			if(isPassed == false)
			{
				// 特性
				{
					int ability = 0;
					if(l_Pokemon[0].abilityFlag == 3)
					{
						ability = xoroshiro.Next(1);
					}
					else
					{
						do {
							ability = xoroshiro.Next(3);
						} while(ability >= 3);
					}
					if((l_Pokemon[0].ability >= 0 && l_Pokemon[0].ability != ability) || (l_Pokemon[0].ability == -1 && ability >= 2))
					{
						continue;
					}
				}

				// 性別値
				if(!l_Pokemon[0].isNoGender)
				{
					int gender = 0;
					do {
						gender = xoroshiro.Next(0xFF); // 性別値
					} while(gender >= 253);
				}

				int nature = 0;
				do {
					nature = xoroshiro.Next(0x1F); // 性格
				} while(nature >= 25);

				if(nature != l_Pokemon[0].nature)
				{
					continue;
				}
			}
		}

		// 2匹目
		_u64 nextSeed = seed + 0x82a2b175229d6a5bull;
		{
			xoroshiro.SetSeed(nextSeed);
			xoroshiro.Next(); // EC
			xoroshiro.Next(); // OTID
			xoroshiro.Next(); // PID

			int vCount = l_Pokemon[1].flawlessIvs;

			// 個体値計算
			int ivs[6] = { -1, -1, -1, -1, -1, -1 };
			int fixedCount = 0;
			do {
				int fixedIndex = 0;
				do {
					fixedIndex = xoroshiro.Next(7); // V箇所
				} while(fixedIndex >= 6);

				if(ivs[fixedIndex] == -1)
				{
					ivs[fixedIndex] = 31;
					++fixedCount;
				}
			} while(fixedCount < vCount);

			// 個体値チェック
			bool isPassed = true;
			for(int i = 0; i < 6; ++i)
			{
				if(ivs[i] == 31)
				{
					if(l_Pokemon[1].ivs[i] != 31)
					{
						isPassed = false;
						break;
					}
				}
				else if(l_Pokemon[1].ivs[i] != xoroshiro.Next(0x1F))
				{
					isPassed = false;
					break;
				}
			}
			if(!isPassed)
			{
				continue;
			}

			// イベントレイドの夢特性強制モード
			isPassed = false;
			if(l_Pokemon[1].abilityFlag == 2 && l_Pokemon[1].ability == 2)
			{
				oshiroTemp2.Copy(&xoroshiro);

				// 特性スキップ

				// 性別値
				if(!l_Pokemon[1].isNoGender)
				{
					int gender = 0;
					do {
						gender = xoroshiro.Next(0xFF); // 性別値
					} while(gender >= 253);
				}

				int nature = 0;
				do {
					nature = xoroshiro.Next(0x1F); // 性格
				} while(nature >= 25);

				if(nature == l_Pokemon[1].nature)
				{
					isPassed = true;
				}

				xoroshiro.Copy(&oshiroTemp2);
			}
			if(isPassed == false)
			{
				// 特性
				int ability = 0;
				if(l_Pokemon[1].abilityFlag == 3)
				{
					ability = xoroshiro.Next(1);
				}
				else
				{
					do {
						ability = xoroshiro.Next(3);
					} while(ability >= 3);
				}
				if((l_Pokemon[1].ability >= 0 && l_Pokemon[1].ability != ability) || (l_Pokemon[1].ability == -1 && ability >= 2))
				{
					continue;
				}

				// 性別値
				if(!l_Pokemon[1].isNoGender)
				{
					int gender = 0;
					do {
						gender = xoroshiro.Next(0xFF);
					} while(gender >= 253);
				}

				// 性格
				int nature = 0;
				do {
					nature = xoroshiro.Next(0x1F);
				} while(nature >= 25);

				if(nature != l_Pokemon[1].nature)
				{
					continue;
				}
			}
		}
		// 3匹目チェック
		if(g_isEnableThird)
		{
			nextSeed = nextSeed + 0x82a2b175229d6a5bull;
			{
				xoroshiro.SetSeed(nextSeed);
				xoroshiro.Next(); // EC
				xoroshiro.Next(); // OTID
				xoroshiro.Next(); // PID

				int vCount = l_Pokemon[2].flawlessIvs;

				// 個体値計算
				int ivs[6] = { -1, -1, -1, -1, -1, -1 };
				int fixedCount = 0;
				do {
					int fixedIndex = 0;
					do {
						fixedIndex = xoroshiro.Next(7); // V箇所
					} while(fixedIndex >= 6);

					if(ivs[fixedIndex] == -1)
					{
						ivs[fixedIndex] = 31;
						++fixedCount;
					}
				} while(fixedCount < vCount);

				// 個体値チェック
				bool isPassed = true;
				for(int i = 0; i < 6; ++i)
				{
					if(ivs[i] == 31)
					{
						if(l_Pokemon[2].ivs[i] != 31)
						{
							isPassed = false;
							break;
						}
					}
					else if(l_Pokemon[2].ivs[i] != xoroshiro.Next(0x1F))
					{
						isPassed = false;
						break;
					}
				}
				if(!isPassed)
				{
					continue;
				}

				// イベントレイドの夢特性強制モード
				isPassed = false;
				if(l_Pokemon[2].abilityFlag == 2 && l_Pokemon[2].ability == 2)
				{
					oshiroTemp2.Copy(&xoroshiro);

					// 特性スキップ

					// 性別値
					if(!l_Pokemon[2].isNoGender)
					{
						int gender = 0;
						do {
							gender = xoroshiro.Next(0xFF); // 性別値
						} while(gender >= 253);
					}

					int nature = 0;
					do {
						nature = xoroshiro.Next(0x1F); // 性格
					} while(nature >= 25);

					if(nature == l_Pokemon[2].nature)
					{
						isPassed = true;
					}

					xoroshiro.Copy(&oshiroTemp2);
				}
				if(isPassed == false)
				{
					// 特性
					int ability = 0;
					if(l_Pokemon[2].abilityFlag == 3)
					{
						ability = xoroshiro.Next(1);
					}
					else
					{
						do {
							ability = xoroshiro.Next(3);
						} while(ability >= 3);
					}
					if((l_Pokemon[2].ability >= 0 && l_Pokemon[2].ability != ability) || (l_Pokemon[2].ability == -1 && ability >= 2))
					{
						continue;
					}

					// 性別値
					if(!l_Pokemon[2].isNoGender)
					{
						int gender = 0;
						do {
							gender = xoroshiro.Next(0xFF);
						} while(gender >= 253);
					}

					// 性格
					int nature = 0;
					do {
						nature = xoroshiro.Next(0x1F);
					} while(nature >= 25);

					if(nature != l_Pokemon[2].nature)
					{
						continue;
					}
				}
			}
		}
		return seed;
	}
	return 0;
}
