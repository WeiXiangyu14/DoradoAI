#include "sdk.h"
#include "const.h"
#include "console.h"
#include "filter.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <cmath>
using namespace std;

//几种变形：推家受挫，是先采矿等增加人数后再推，还是无脑猛推，还是重新集合一下用现有人手继续推，还是先采矿，人全复活继续推（最后一条没写）
//			开始防御后，防完一波继续死守拼经济，还是死守等攒够八人推家，还是立刻反推。是绕路推家还是直接上
//现在：攒够6人开推，受挫则攒经济加人，防守就死守到8人再去推。遇上无脑猛推的，可能在防守转进攻时被趁虚而入

static int mySide = 0;//我是哪一方？
static int heroRank = 2431;
static int newHeroFirst = 1;
static int attackArg = 200, supportRange = 1000, monsterAvoid = 0, enemyAvoid = 30, deltaFightArg = 100;
static int outOfRangeArg = 100;
static int miningArg = 140, sameMineArg = 15, centerMineArg = 50,secondMineArg = 15;
static int goBackHomeHp = 100, goBackHomeArg = 300;
static int hammerDizzy = 50;
static int temptArg = 200 ;
static int strategyDisabled = - 1<<30;
static int strategyAbled = 1<<30;

static int killRevive = 300 ;//击杀涅槃中的Berserker
static const char* HERO_NAME[] = {"Hammerguard", "Master", "Berserker", "Scouter"};
static int heroNum = 0;//我方人数
static int gatherRange = 300 ;//集合判定范围

static int sneakNum = 6;//达到该人数则去偷家

static int generalStrategy = 0;//总策略，为0则正常，令每个单位按照策略行动
							   //为1：去换一个矿
							   //为2：去偷家
static int gatherSucceed = 0;// 为0：集合中；为1：集合完毕
static int attackReady = 0;// 为0：准备中；为1：准备完毕
static int enemyAttackHome = 0;//为1：敌人开始偷家
static int enemyAttackHomeOver = 0;//为1：防下来一波

static int attackBaseX = 120;
static int attackBaseY = 120;

static int mineNow = 0;//现在的目标矿，0（我方为0时）
static int mineNext = 1 ;//下一个目标矿，1（我方为0时）
static int mineFinal = 6 ;//失败时的选择

static Pos colPos[] = {Pos(65, 65), Pos(35, 40), Pos(35, 90), Pos(115, 60), Pos(115, 90), Pos(20, 140), Pos(125, 25)};
static int colSucceed = 0;// 为0：集合中；为1：集合完毕

static int enemyAtHome = 0;


bool isActiveSkill(PSkill* mySkill) //判断技能是否为主动技能
{
	return !strcmp(mySkill->name, "HammerAttack")
		|| !strcmp(mySkill->name, "Blink")
		|| !strcmp(mySkill->name, "Sacrifice")
		|| !strcmp(mySkill->name, "SetObserver");
}

class AIController //控制ai的主类
{
public:
	AIController(const PMap &map, const PPlayerInfo &info, PCommand &cmd);
	~AIController();
public:
	void levelupHero();
	void buyNewHero();
	void action();
	void gatherHeros(int x,int y);//集合英雄干一些事情
	void attackBase();//攻击敌方基地
	void AttackPrepare();//攻击准备
	void SetGeneralStrategy();//判断总策略
private:
	Console* console;
    const PMap* map;
    const PPlayerInfo* info;
    PCommand* cmd;
	std::vector<PUnit*> myHeros, enemyHeros;
	PUnit *myBase, *enemyBase;
};

class AIHero; //前向引用声明

class Strategy //策略基类
{
public:
	virtual int countWorth() = 0; //计算某种策略的实行价值
	virtual void work() = 0; //实现这种策略
public:
	void setWorker(PUnit* worker) {this->worker = worker;}
	int getWorth() {return worth;}
	void setWorth(int worth) {this->worth = worth;}
	PUnit* getWorker() {return worker;}
	string getName() {return name;}
	void setAIHeroList(vector<AIHero*>& AIHeroList)
	{
		for(int i = 0; i < AIHeroList.size(); ++i)
			this->AIHeroList.push_back(AIHeroList[i]);
	}
	void setConsole(Console* console) {this->console = console;}
protected:
	PUnit* worker;
	int worth;
	vector<AIHero*> AIHeroList;
	Console* console;
	string name;
};

class Attack:public Strategy //攻击某个单位的策略
{
public:
	Attack(PUnit* worker,PUnit* target) {setWorker(worker);setTarget(target);name = "Attack";}
public:
	virtual int countWorth();
	virtual void work();
	void setTarget(PUnit* target) {this->target = target;}
private:
	PUnit* target;
};

class UseSkill:public Strategy //对某个单位释放技能的策略
{
public:
	UseSkill(PUnit* worker,PUnit* target) {setWorker(worker);setTarget(target);name = "UseSkill";}
public:
	virtual int countWorth();
	virtual void work();
	void setTarget(PUnit* target) {this->target = target;}
private:
	PUnit* target;
	Pos targetPos;
};

class GoBackHome:public Strategy //英雄步行回家的策略
{
public:
	GoBackHome(PUnit* worker) {setWorker(worker);name = "GoBackHome";}
public:
	virtual int countWorth();
	virtual void work();
};

class CallBackHome:public Strategy //召回英雄的策略
{
public:
	CallBackHome(PUnit* worker) {setWorker(worker);name = "CallBackHome";}
public:
	virtual int countWorth();
	virtual void work();
};

class GoMining:public Strategy //采矿的策略
{
public:
	GoMining(PUnit* worker, Pos target) {setWorker(worker);setTarget(target);name = "GoMining";}
public:
	virtual int countWorth();
	virtual void work();
	void setTarget(Pos target) {this->target = target;}
	Pos getTarget() {return target;}
private:
	Pos target;
};

class Tempt:public Strategy //引诱敌人（野怪）的策略-即几乎保证自己不受伤的情况下进行hit-and-run
{
public:
	Tempt(PUnit* worker) {setWorker(worker);name = "Tempt";}
public:
	virtual int countWorth();
	virtual void work();
};

class Tantalize:public Strategy //残血英雄调戏对手的策略
{
public:
	Tantalize(PUnit* worker) {setWorker(worker);name = "Tantalize";}
public:
	virtual int countWorth();
	virtual void work();
};

class HitRoshan:public Strategy //打肉山
{
public:
	HitRoshan(PUnit* worker) {setWorker(worker);name = "HitRoshan";}
public:
	virtual int countWorth();
	virtual void work();
};

class HomeDefence:public Strategy //防偷家
{
public:
	HomeDefence(PUnit* worker) {setWorker(worker);name = "HomeDefence";}
public:
	virtual int countWorth();
	virtual void work();
};

class TryHome:public Strategy //骚扰家
{
public:
	TryHome(PUnit* worker) {setWorker(worker);name = "TryHome";}
public:
	virtual int countWorth();
	virtual void work();
	PUnit* target;
};

class AIHero //对应每个英雄的策略决策
{
public:
	AIHero(PUnit* newHero) {mHero = newHero; mStrategy = NULL;} //默认选择回家的策略
	~AIHero() {if(mStrategy != NULL) delete mStrategy;}
public:
	bool setStrategy(Strategy* newStrategy); //根据策略的价值判断是否选择新的策略
	void action(); //进行当前的最优策略
	Strategy* getStrategy() {return mStrategy;}
private:
	PUnit* mHero;
	Strategy* mStrategy;
};

AIController::AIController(const PMap &map, const PPlayerInfo &info, PCommand &cmd)
{
	console = new Console(map,info,cmd);
    this->map = &(map);
    this->info = &(info);
    this->cmd = &(cmd);
    mySide=console->camp();

	myHeros.clear();
	std::vector<PUnit*> myUnits = console->friendlyUnits();
	std::vector<PUnit*> enemyUnits = console->enemyUnits();
	enemyBase = NULL;
	for(int i = 0; i < myUnits.size(); ++i)
	{
		if(!strcmp(myUnits[i]->name,"Hammerguard") || !strcmp(myUnits[i]->name,"Master")
		|| !strcmp(myUnits[i]->name,"Berserker") || !strcmp(myUnits[i]->name,"Scouter"))
			myHeros.push_back(myUnits[i]);
		else if(!strcmp(myUnits[i]->name,"MilitaryBase"))
			myBase = myUnits[i];
	}
	for(int i = 0; i < enemyUnits.size(); ++i)
	{
		if(!strcmp(enemyUnits[i]->name,"Hammerguard") || !strcmp(enemyUnits[i]->name,"Master")
		|| !strcmp(enemyUnits[i]->name,"Berserker") || !strcmp(enemyUnits[i]->name,"Scouter")
		|| !strcmp(enemyUnits[i]->name,"Dragon") || !strcmp(enemyUnits[i]->name,"Roshan"))
			enemyHeros.push_back(enemyUnits[i]);
		else if(!strcmp(enemyUnits[i]->name,"MilitaryBase"))
			enemyBase = enemyUnits[i];
	}

/*	if(info.round>2)
	{
		cout<<myHeros[0]->name<<" to cenmine :"<<dis2(myHeros[0]->pos,MINE_POS[mineNow])<<endl;
		cout<<myHeros[0]->name<<" to home :"<<dis2(myHeros[0]->pos,myBase->pos)<<endl;
	}
	*/

	heroNum=myHeros.size();
	//if(heroNum<7)
	//	monsterAvoid = 3000;
	//else
	//{
	//	sameMineArg = 20 ;
	//	monsterAvoid = 60 ;
	//}

	//统计回合数？
	//一定回合后开始考虑分矿，一开始只采主矿

}

AIController::~AIController()
{
//	cout<<"myHeros:"<<myHeros.size()<<endl;
	delete console;
}

void AIController::levelupHero()
{
	UnitFilter filter;
	filter.setAreaFilter (new Circle (myBase->pos, LEVELUP_RANGE ), "a");
	vector<PUnit*> heroToLevelUp = console->friendlyUnits (filter);
	for(int i = 0; i < heroToLevelUp.size(); ++i)
		console->buyHeroLevel(heroToLevelUp[i]);
}

void AIController::buyNewHero()
{
//没有判断是否召唤成功，故如果没成功召唤顺序就会乱掉
	int newHero = heroRank % 10 - 1;
	if(newHero >= 0 && newHero < 4) 
	{
		console->chooseHero(HERO_NAME[newHero]);
		heroRank /= 10;
	}
	else//造当前最少的英雄
	{
		std::vector<PUnit*> myUnits = console->friendlyUnits();
		int HG=0,Ma=0,Bk=0,St=0;
		for(int i = 0; i < myUnits.size(); ++i)
		{
			if(!strcmp(myUnits[i]->name,"Hammerguard") ) HG++;
			if(!strcmp(myUnits[i]->name,"Master") ) Ma++;
			if(!strcmp(myUnits[i]->name,"Berserker") ) Bk++;
			if(!strcmp(myUnits[i]->name,"Scouter") ) St++;

		}
		if (HG<=Ma&&HG<=Bk&&HG<=St) console->chooseHero("Hammerguard");//此顺序可调换
		else if (Bk<=HG&&Bk<=Ma&&Bk<=St) console->chooseHero("Berserker");
		else if (Ma<=HG&&Ma<=Bk&&Ma<=St) console->chooseHero("Master");
		else if (St<=HG&&St<=Bk&&St<=Ma) console->chooseHero("Scouter");


		//heroRank=3214;//
		//newHero = heroRank % 10 - 1;
		//console->chooseHero(HERO_NAME[newHero]);
		//newHero = rand() % 4;
		//console->chooseHero(HERO_NAME[newHero]);
	}
	
}

void AIController::gatherHeros(int x,int y)
{
	
	if(gatherSucceed == 0 && generalStrategy !=0 )
	{
		Pos gatherPoint(x,y);
		for(int i = 0; i < myHeros.size(); ++i)
		{
			console->selectUnit(myHeros[i]);
			/*
			UnitFilter filter;
			filter.setAreaFilter (new Circle (myHeros[i]->pos, 144 ), "a");
			vector<PUnit*> enemyDisturb = console->enemyUnits (filter);
			int cntDisturb=0;
			for(int i=0;i<enemyDisturb.size();i++)
			{
				if(!strcmp(enemyDisturb[i]->name,"Hammerguard") || !strcmp(enemyDisturb[i]->name,"Master")
				|| !strcmp(enemyDisturb[i]->name,"Berserker") || !strcmp(enemyDisturb[i]->name,"Scouter")
				|| !strcmp(enemyDisturb[i]->name,"Dragon") || !strcmp(enemyDisturb[i]->name,"Roshan"))
					if(enemyDisturb[i]->findBuff("Reviving") == NULL)
						cntDisturb++;
			}


			if(cntDisturb>0 && dis2(myHeros[i]->pos,gatherPoint) < gatherRange)
			{
				for(int i=0;i<enemyDisturb.size();i++)
				{
					if(!strcmp(enemyDisturb[i]->name,"Hammerguard") || !strcmp(enemyDisturb[i]->name,"Master")
						|| !strcmp(enemyDisturb[i]->name,"Berserker") || !strcmp(enemyDisturb[i]->name,"Scouter")
						|| !strcmp(enemyDisturb[i]->name,"Dragon") || !strcmp(enemyDisturb[i]->name,"Roshan"))
						if(enemyDisturb[i]->findBuff("Reviving") == NULL)
							break;
				}
				if(enemyDisturb[i]!=NULL)
					console->attack(enemyDisturb[i]);
				else
					console->move(gatherPoint);
			}
			else*/
				console->move(gatherPoint);
		}
		UnitFilter filter;
		filter.setAreaFilter (new Circle (gatherPoint, gatherRange ), "a");

		vector<PUnit*> heroGathered = console->friendlyUnits (filter);

		if (heroGathered.size()+2>myHeros.size())//人数够了
		{
			gatherSucceed=1;
		}
	}
}

void AIController::AttackPrepare()
{
	if (mySide==0)
	{
		attackBaseX=140;
		attackBaseY=120;
	}
	else if (mySide==1)
	{
		attackBaseX=10;
		attackBaseY=30;
	}
	Pos tmpPoint(attackBaseX,attackBaseY);
	for(int i = 0; i < myHeros.size(); ++i)
	{
		console->selectUnit(myHeros[i]);
		/*
			UnitFilter filter;
			filter.setAreaFilter (new Circle (myHeros[i]->pos, 144 ), "a");
			vector<PUnit*> enemyDisturb = console->enemyUnits (filter);
			int cntDisturb=0;
			for(int i=0;i<enemyDisturb.size();i++)
			{
				if(!strcmp(enemyDisturb[i]->name,"Hammerguard") || !strcmp(enemyDisturb[i]->name,"Master")
				|| !strcmp(enemyDisturb[i]->name,"Berserker") || !strcmp(enemyDisturb[i]->name,"Scouter")
				|| !strcmp(enemyDisturb[i]->name,"Dragon") || !strcmp(enemyDisturb[i]->name,"Roshan"))
					if(enemyDisturb[i]->findBuff("Reviving") == NULL)
						cntDisturb++;
			}


			if(cntDisturb>0 && dis2(myHeros[i]->pos,tmpPoint) < gatherRange)
			{
				for(int i=0;i<enemyDisturb.size();i++)
				{
					if(!strcmp(enemyDisturb[i]->name,"Hammerguard") || !strcmp(enemyDisturb[i]->name,"Master")
						|| !strcmp(enemyDisturb[i]->name,"Berserker") || !strcmp(enemyDisturb[i]->name,"Scouter")
						|| !strcmp(enemyDisturb[i]->name,"Dragon") || !strcmp(enemyDisturb[i]->name,"Roshan"))
						if(enemyDisturb[i]->findBuff("Reviving") == NULL)
							break;
				}
				if(enemyDisturb[i]!=NULL)
					console->attack(enemyDisturb[i]);
				else
					console->move(tmpPoint);
			}
			else*/
				console->move(tmpPoint);
	}

	UnitFilter filter;
	filter.setAreaFilter (new Circle (tmpPoint , gatherRange/3 ), "a");

	vector<PUnit*> heroReady = console->friendlyUnits (filter);

	if (heroReady.size()+2>myHeros.size())//进攻的人够了
	{
		attackReady= 1 ;
	}
}

void AIController::attackBase()
{
	for(int i = 0; i < myHeros.size(); ++i)
	{
		Pos tmpPoint0(137,141);
		Pos tmpPoint1(13,9);
		console->selectUnit(myHeros[i]);
		if (mySide==0)
			console->move(tmpPoint0);
		else if (mySide==1)
			console->move(tmpPoint1);
		if(enemyBase!=NULL)
			console->attack(enemyBase);
	}

	if(enemyBase!=NULL)
	{
		UnitFilter filter;
		filter.setAreaFilter (new Circle (enemyBase->pos, 500 ), "a");

		vector<PUnit*> heroLast = console->friendlyUnits (filter);
		int cntLast=0;
		for(int i=0;i<heroLast.size();i++)
		{
			if(!strcmp(heroLast[i]->name,"Hammerguard") || !strcmp(heroLast[i]->name,"Master")
			|| !strcmp(heroLast[i]->name,"Berserker") || !strcmp(heroLast[i]->name,"Scouter"))
				if(heroLast[i]->findBuff("Reviving") == NULL)
					cntLast++;

		}


		if(cntLast<=3 && enemyAttackHome == 0)//停止偷家
		{
			if(cntLast<=1 || enemyBase->max_hp/enemyBase->hp < 7)
			{ 
				generalStrategy=0;
				gatherSucceed=0;
				attackReady=0 ;
			}

			if(enemyBase->max_hp/enemyBase->hp < 2)
			{
				if(sneakNum<8)
					sneakNum++;
			}

/*//打yangjing14
			enemyAtHome = 1;
			generalStrategy=0;
			gatherSucceed=0;
			attackReady=0 ;
			mineNow = 0;
			mineFinal = 0;
*/	
		}
	}
}

void AIController::SetGeneralStrategy()
{

	UnitFilter efilter;
	efilter.setAreaFilter (new Circle (console->getMilitaryBase()->pos, 300 ), "a");
	vector<PUnit*> enemyAtBase = console->enemyUnits (efilter);

	if(myHeros.size() >= sneakNum && enemyAtBase.size() == 0)
		generalStrategy=2;

	if(enemyAtBase.size()>1 && attackReady == 0 )//敌人打来，自己没有开始进攻，则进入防守模式，放弃进攻
	{
		generalStrategy = 0;
	//	gatherSucceed = 1;
	//	attackReady = 1 ;
	//	mineNow = mineNext;
	//	mineFinal = mineNext;
		mineNow = 0;
		enemyAttackHome = 1;
		sneakNum = 8 ;
	}

//	cout<<"generalStrategy "<<generalStrategy<<endl;
//	cout<<"enemyAttackHome "<<enemyAttackHome<<endl;
//	cout<<"enemyAtBase.size() "<<enemyAtBase.size()<<endl<<endl;

	if(enemyAttackHome == 1 && enemyAtBase.size() == 0)
	{
		enemyAttackHomeOver = 1;
	}

	if(enemyAttackHomeOver == 1)
	{
//		generalStrategy = 2;
//		gatherSucceed = 1;
//		attackReady = 1 ;
	}
}


void AIController::action()
{
	if (mySide==0)
	{
		mineFinal=6;
		mineNext=1;
	}
	else if (mySide==1)
	{
		mineFinal=5;
		mineNext=4;
	}

	if(console->round()>180 && myHeros.size()<5 && enemyAtHome == 0)
		mineNow=mineFinal;


	SetGeneralStrategy();

	





	if(generalStrategy == 0)//大家正常行动
	{
		vector<Strategy*> allStrategy;
		vector<AIHero*> AIHeroListTmp;

		for(int i = 0; i < myHeros.size(); ++i) AIHeroListTmp.push_back(new AIHero(myHeros[i]));

		for(int i = 0; i < myHeros.size(); ++i)
			for(int j = 0; j < enemyHeros.size(); ++j)
				allStrategy.push_back(new UseSkill(myHeros[i], enemyHeros[j]));
		for(int i = 0; i < myHeros.size(); ++i)
			for(int j = 0; j < enemyHeros.size(); ++j)
				allStrategy.push_back(new Attack(myHeros[i], enemyHeros[j]));

	//	for(int i = 0; i < myHeros.size(); ++i)
	//		for(int j = 0; j < enemyHeros.size(); ++j)
	//			allStrategy.push_back(new Tempt(myHeros[i], enemyHeros[j]));

		for(int i = 0; i < myHeros.size(); ++i) allStrategy.push_back(new HomeDefence(myHeros[i]));
		for(int i = 0; i < myHeros.size(); ++i)
		{
			if(!strcmp(myHeros[i]->name, "Scouter"))
				allStrategy.push_back(new Tempt(myHeros[i]));
		} 
		for(int i = 0; i < myHeros.size(); ++i)
		{
			if(!strcmp(myHeros[i]->name, "Scouter"))
				allStrategy.push_back(new TryHome(myHeros[i]));
		} 
		for(int i = 0; i < myHeros.size(); ++i) allStrategy.push_back(new GoBackHome(myHeros[i]));
		for(int i = 0; i < myHeros.size(); ++i) allStrategy.push_back(new CallBackHome(myHeros[i]));
		for(int i = 0; i < myHeros.size(); ++i)
			for(int j = 0; j < MINE_NUM; ++j)
				allStrategy.push_back(new GoMining(myHeros[i],MINE_POS[j]));

		for(int i = 0; i < allStrategy.size(); ++i)
			allStrategy[i]->setAIHeroList(AIHeroListTmp);
		for(int i = 0; i < allStrategy.size(); ++i)
			allStrategy[i]->setConsole(console);

		//	如下，所有单位均执行所有策略的最大值，是否会出现：
		//	单位a的放技能策略>……>单位b的回家策略>……>单位b的放技能策略
		//	这样，单位b也会执行放技能策略，但会在setStrategy函数中判断执行者时不成功，return false，不更新其mStrategy
		//	实际上对于单位b来说回家更好

		for(int i = 0; i < allStrategy.size(); ++i)
			for(int j = 0; j < AIHeroListTmp.size(); ++j)
				AIHeroListTmp[j]->setStrategy(allStrategy[i]);
		for(int i = 0; i < AIHeroListTmp.size(); ++i)
			AIHeroListTmp[i]->action();

		for(int i = 0; i < AIHeroListTmp.size(); ++i)
			if(AIHeroListTmp[i] != NULL)
				delete AIHeroListTmp[i];
	}

	else if(generalStrategy == 1)//换个矿
	{//暂时用来调试
		
	}

	else if(generalStrategy == 2)//偷家
	{
		if (gatherSucceed == 0)
		{
			if(mySide==0)
				gatherHeros(120,30);
			else if(mySide==1)
				gatherHeros(30,120);
		}

		if (gatherSucceed == 1)
		{
			if(attackReady==0)
			{
			//	cout<<"Preparing"<<endl;
				AttackPrepare();
			}
			if(attackReady==1)
			{
		//		cout<<"Ready"<<endl;
				attackBase();
			}
		}
	}




}

bool AIHero::setStrategy(Strategy* newStrategy)
{
	if(newStrategy == NULL) return false;
	if(newStrategy->getWorker() != this->mHero) return false;
	if(mStrategy == NULL || newStrategy->countWorth() > this->mStrategy->countWorth())
	{
		if(mStrategy != NULL)
			delete mStrategy;
		mStrategy = newStrategy;
		return true;
	}
	else
		delete newStrategy;
	return false;
}

void AIHero::action()
{
	mStrategy->work();
}

int Attack::countWorth()
{
	this->worth = 0;
	if(target->findBuff("Reviving") != NULL)
		this->worth = strategyDisabled;
	if(!strcmp(target->name, "Observer"))
		this->worth = strategyDisabled;
	

	else if(dis2(worker->pos,target->pos) <= supportRange)
	{
		if(target->findBuff("WaitRevive") != NULL)
			this->worth += killRevive;

		UnitFilter filter;
		filter.setAreaFilter (new Circle (target->pos, supportRange ), "a");

		this->worth += ((double)target->max_hp/(double)target->hp) * (double)attackArg;
		if(dis2(worker->pos,target->pos) > worker->range)
			this->worth -= outOfRangeArg;
		if(target->isWild()) this->worth -= monsterAvoid;
	}
	return this->worth;
}

void Attack::work()
{
	console->selectUnit(worker);
	console->attack(target);
}

int UseSkill::countWorth()
{
	this->worth = 0;
	console->selectUnit(worker);
	vector<PSkill*> skillList = console->getSkills();

//	for(int i = 0; i < skillList.size(); ++i)
//		cout<<skillList[i]->name<<endl;//skillList是什么？这个单位的所有技能吗？

	for(int i = 0; i < skillList.size(); ++i)
		if(isActiveSkill(skillList[i]))
		{
			if(!worker->canUseSkill(skillList[i]))
				this->worth = strategyDisabled;//执行一次这个if-else就break了，也就是说
											   //找到第一个主动技能就试着实行？如果找到的技能
											   //不是该英雄的技能会怎么样？
			else
			{
				//此处写对技能的处理，由于样例ai故非常简略
				if(!strcmp(skillList[i]->name, "HammerAttack"))
				{
					if(target->findBuff("Reviving") != NULL)
						this->worth = strategyDisabled;
					if(!strcmp(target->name, "Observer"))
						this->worth = strategyDisabled;

					if(dis2(worker->pos,target->pos) <= supportRange)
					{
						this->worth += ((double)target->max_hp/(double)target->hp) * (double)attackArg;
						if(dis2(worker->pos,target->pos) > HAMMERATTACK_RANGE)
							this->worth -= outOfRangeArg;
						if(target->findBuff("Dizzy") != NULL)
							this->worth -= hammerDizzy;
					}
				}
				else if(!strcmp(skillList[i]->name, "Sacrifice"))//能否记录上一回合信息，从而判断是否sacrifice
				{
					if(target->findBuff("Reviving") != NULL)
						this->worth = strategyDisabled;
					if(!strcmp(target->name, "Observer"))
						this->worth = strategyDisabled;
					if(worker->findBuff("BeAttacked") != NULL)
						this->worth = strategyDisabled;

					if(dis2(worker->pos,target->pos) <= supportRange/4)
					{
						this->worth += ((double)target->max_hp/(double)target->hp) * (double)attackArg * 2;
						if(dis2(worker->pos,target->pos) > worker->view)
							this->worth -= outOfRangeArg * 2;
						if(target->findBuff("Dizzy") != NULL)
							this->worth += hammerDizzy;
						if(worker->findBuff("WinOrDie") != NULL)
							this->worth += (144-dis2(worker->pos,target->pos));

					}
				}
				else if(!strcmp(skillList[i]->name, "SetObserver"))
				{
					if(target->findBuff("Reviving") != NULL)
						this->worth = strategyDisabled;
					if(dis2(worker->pos,target->pos) <= 2*supportRange/3)
					{
						this->worth += 2 * attackArg;
						if(dis2(worker->pos,target->pos) > worker->view)
							this->worth -= outOfRangeArg * 2;
						if(target->findBuff("Dizzy") != NULL)
							this->worth -= hammerDizzy;

						int xx= worker->pos.x + (target->pos.x - worker->pos.x)/2;
						int yy= worker->pos.y + (target->pos.y - worker->pos.y)/2;
						Pos locOfObserver(xx,yy);
						//this->targetPos = console->randPosInArea(worker->pos,skillList[i]->range());
						this->targetPos = locOfObserver;
					}
				}
			/*	else if(!strcmp(skillList[i]->name, "Blink"))
				{
					if(target->findBuff("Reviving") != NULL)
						this->worth = strategyDisabled;

					//刚出生时多跳几下，跑得慢

					if(dis2(worker->pos,target->pos) <= worker->range/2 && worker->max_hp/worker->hp >=2)//半血以下
					{
						this->worth += 2 * attackArg;
						
						if(target->findBuff("Dizzy") != NULL)
							this->worth -= hammerDizzy;

						int xx= worker->pos.x - (target->pos.x - worker->pos.x);
						int yy= worker->pos.y - (target->pos.y - worker->pos.y);
						Pos locOfBlink(xx,yy);
						//this->targetPos = console->randPosInArea(worker->pos,skillList[i]->range());
						this->targetPos = locOfBlink;
					}
				}*/
			}
			break;
		}
	if(target->isWild()) 
		this->worth -= monsterAvoid;
	//cout<<"Skillworth: "<<this->worth<<endl;
	return this->worth;
}

void UseSkill::work()
{
	console->selectUnit(worker);
	vector<PSkill*> skillList = console->getSkills();
	for(int i = 0; i < skillList.size(); ++i)
		if(isActiveSkill(skillList[i]))
		{
			if(!strcmp(skillList[i]->name, "HammerAttack"))
				console->useSkill(skillList[i], target);
			else if(!strcmp(skillList[i]->name, "Sacrifice"))
				console->useSkill(skillList[i], NULL);
			else
				console->useSkill(skillList[i], targetPos);
			break;
		}
}
///////////////////////////////////////////////////////////////////////////////////
//																
//		  ////////////////		   /////	 /////		  //////////////		
//	     ////////////////			/////	/////		  ///////////////								
//					////			 ///// /////		  ////		 ////				
//				   ////				  /////////			  ////   	 ////	
//				  ////				   ///////		 	  //////////////		
//				 ////				   //////			  ////////////				
//				////				  ////////			  /////  /////					
//			   ////					 //////////			  ////    /////																				//
//			  ////			        /////  /////		  ////     /////				
//			 //////////////		   /////	/////		  ////	    /////							
//			//////////////		  /////		 /////		  ////	     /////						
//																	
///////////////////////////////////////////////////////////////////////////////////
int Tempt::countWorth()//非常粗糙，只考虑了1对1的情况，应把多敌人加入考虑
{
	this->worth = 0;
	if(strcmp(worker->name,"Scouter"))//不是Scouter
	{
		this->worth = strategyDisabled;
		return this->worth;
	}

	UnitFilter efilter;
	efilter.setAreaFilter (new Circle (worker->pos, worker->view ), "a");
	vector<PUnit*> enemyAtScouter = console->enemyUnits (efilter);

	UnitFilter ffilter;
	ffilter.setAreaFilter (new Circle (worker->pos, worker->view ), "a");
	vector<PUnit*> friendsAtScouter = console->friendlyUnits (efilter);

	int enemycnt=0;
	for(int i = 0; i < enemyAtScouter.size(); ++i)
	{
		if((!strcmp(enemyAtScouter[i]->name,"Hammerguard") || !strcmp(enemyAtScouter[i]->name,"Master")
		|| !strcmp(enemyAtScouter[i]->name,"Berserker") || !strcmp(enemyAtScouter[i]->name,"Scouter")) && enemyAtScouter[i]->findBuff("Reviving")==NULL)
			enemycnt++;
	}
	int friendscnt=0;
	for(int i = 0; i < friendsAtScouter.size(); ++i)
	{
		if((!strcmp(friendsAtScouter[i]->name,"Hammerguard") || !strcmp(friendsAtScouter[i]->name,"Master")
		|| !strcmp(friendsAtScouter[i]->name,"Berserker") ||!strcmp(friendsAtScouter[i]->name,"MilitaryBase")) && friendsAtScouter[i]->findBuff("Reviving")==NULL)
			friendscnt++;
	}

	if(enemycnt!=0 && friendscnt == 0)
		this->worth = attackArg*3;

	if(dis2(worker->pos,MINE_POS[mineNow]) < 144)
		this->worth = 0;



/*	if(dis2(worker->pos,target->pos) <= supportRange)
	{
		this->worth += ((double)target->max_hp/(double)target->hp) * (double)attackArg;
		if(dis2(worker->pos,target->pos) > worker->range)
			this->worth -= outOfRangeArg;
		if(target->isWild()) this->worth -= monsterAvoid;
	} 
	else
	{
		if(!strcmp(target->name,"Roshan") )
		this->worth += temptArg;
	}
	this->worth -= 100 ;*/



	return this->worth;
}

void Tempt::work()
{
	console->selectUnit(worker);
	console->move(MINE_POS[mineNow]);

//	if(dis2(worker->pos,target->pos) > worker->range/2)
//		console->attack(target);
//	else
//	{
//		int xx=2*worker->pos.x - target->pos.x,yy=2*worker->pos.y - target->pos.y;
//		Pos awayEnemy(xx,yy);
//		console->move(awayEnemy);//往敌人的反方向走
//	}
}

int HitRoshan::countWorth()
{

}

void HitRoshan::work()
{

}

int HomeDefence::countWorth()
{
	this->worth=0;
	UnitFilter efilter;
	efilter.setAreaFilter (new Circle (console->getMilitaryBase()->pos, 144 ), "a");
	vector<PUnit*> enemyAtBase = console->enemyUnits (efilter);

	for(int i=0;i<enemyAtBase.size();i++)
	{
	//	cout<<enemyAtBase[i]->name;
	}

	if(enemyAttackHome == 1 && dis2(worker->pos,console->getMilitaryBase()->pos) > MILITARY_BASE_VIEW)
	{
//		cout<<enemyAtBase.size()<<" ??? "<<endl;
		this->worth += 10000;
	}

	if(!strcmp(worker->name,"Scouter"))
 	{
		if(enemyAttackHomeOver == 1)
			this->worth -= 10000;
	}

	return this->worth;

}

void HomeDefence::work()
{
//	cout<<"worth = "<<this->worth<<endl;
//	cout<<worker->name<<" HomeDefence!!!"<<endl;
	console->selectUnit(worker);
	console->move(console->getMilitaryBase()->pos);
}

int TryHome::countWorth()
{
	this->worth = 0;
	this->target = NULL;
	if(strcmp(worker->name,"Scouter") || console->round() < 40  )//不是Scouter 或 在前183回合
	{
		this->worth = strategyDisabled;
		return this->worth;
	}
	if(double(double(worker->max_hp)/double(worker->hp)) >= 1.5)
	{
		this->worth = strategyDisabled;
		return this->worth;
	}

	UnitFilter efilter;
	efilter.setAreaFilter (new Circle (worker->pos, worker->view ), "a");
	vector<PUnit*> enemyAtScouter = console->enemyUnits (efilter);
	int enemycnt=0;
	for(int i = 0; i < enemyAtScouter.size(); ++i)
	{
		if((!strcmp(enemyAtScouter[i]->name,"Hammerguard") || !strcmp(enemyAtScouter[i]->name,"Master")
		|| !strcmp(enemyAtScouter[i]->name,"Berserker") || !strcmp(enemyAtScouter[i]->name,"Scouter")) && enemyAtScouter[i]->findBuff("Reviving")==NULL)
			enemycnt++;
		else if(!strcmp(enemyAtScouter[i]->name,"MilitaryBase"))
			this->target = enemyAtScouter[i];
	}

	if(enemycnt==0)
		this->worth += 11000;

	return this->worth;

}

void TryHome::work()
{
	Pos tmpPoint0(137,141);
	Pos tmpPoint1(13,9);
	Pos pausepoint0(120,50);
	Pos pausepoint1(30,100);
	console->selectUnit(worker);
	if(target!=NULL)
		console->attack(target);
	else
		if(mySide == 0)
		{
			if(worker->pos.x + worker->pos.y < 160)
				console->move(pausepoint0);
			else
				console->move(tmpPoint0);
		}
		else
		{
			if(worker->pos.x + worker->pos.y >140)
				console->move(pausepoint1);
			else
				console->move(tmpPoint1);
		}

}

int Tantalize::countWorth()
{
	this->worth = 0;
	//想办法统计在上一回合中被集火的英雄，给其设置高worth，未被集火则低worth
	//哪个英雄跑得快？worth更高
	return this->worth;
}

void Tantalize::work()
{
	console->selectUnit((const PUnit*)worker);

	//能否记录敌方英雄的序号？远离攻击自己的英雄

//	console->move(console->getMilitaryBase()->pos);
}

int GoMining::countWorth()
{
	this->worth = miningArg;
	if(target == MINE_POS[mineNow])//目标矿
	{
/*		for(int i = 0; i < AIHeroList.size(); ++i)
			if(AIHeroList[i]->getStrategy() != NULL)
				if(AIHeroList[i]->getStrategy()->getName() == "GoMining")
				{
					GoMining* otherMining = (GoMining*)AIHeroList[i]->getStrategy();
					if(otherMining->getTarget() == this->target)
						this->worth -= sameMineArg;
				}
*/
		this->worth += centerMineArg;
	}
//	if(target == MINE_POS[mineNext])
//		this->worth += secondMineArg;

/*	if(target == MINE_POS[0])
	{
		for(int i = 0; i < AIHeroList.size(); ++i)
			if(AIHeroList[i]->getStrategy() != NULL)
				if(AIHeroList[i]->getStrategy()->getName() == "GoMining")//检验其他我方单位是否在采这个矿
				{
					GoMining* otherMining = (GoMining*)AIHeroList[i]->getStrategy();
					if(otherMining->getTarget() == this->target)
						this->worth -= sameMineArg;
				}
	}
*/
	//if(heroNum>7)
	//{
	//	this->worth -= dis(worker->pos, target);
	//}
	
//	this->worth = 0;

	
//	if(dis2(worker->pos,MINE_POS[mineNow])>supportRange)
//		this->worth += miningArg;
	return this->worth;
}

void GoMining::work()
{
	console->selectUnit(worker);
	console->move(MINE_POS[mineNow]);
/*
	UnitFilter filter;
	filter.setAreaFilter (new Circle (colPos[mineNow], gatherRange ), "a");

	vector<PUnit*> heroGathered = console->friendlyUnits (filter);
	if (heroGathered.size()>2)//人数够了
	{

		colSucceed=1;
	}

	console->selectUnit(worker);
	if(colSucceed)
		console->move(MINE_POS[mineNow]);
	else
		console->move(colPos[mineNow]);
*/
/*	if(strcmp(worker->name, "Master") == 0)
	{
		//int xx=2*worker->pos.x - target.x;
		//int yy=2*worker->pos.y - target.y;

		int d=dis2(worker->pos,target);

		int xl= target.x - worker->pos.x;
		int yl= target.y - worker->pos.y;


		int xx=target.x;
		int yy=target.y;
		Pos pob(xx,yy);
		vector<PSkill*> s = console->getSkills();
		for(int i = 0; i < s.size(); ++i)
		if(isActiveSkill(s[i]) && worker->canUseSkill(s[i]))
		{
			if(!strcmp(s[i]->name, "Blink"))
				console->useSkill(s[i], pob);
			break;
		}
	}*/
/*
	UnitFilter efilter;
	efilter.setAreaFilter (new Circle (worker->pos, 160 ), "a");
	vector<PUnit*> enemyAtMine = console->enemyUnits (efilter);


	
	//cout<<enemyAtMine.size()<<"  "<<myHeroAtMine.size()<<endl;
	int cntEmeny=0;
	for(int i=0;i<enemyAtMine.size();i++)
	{
		if(!strcmp(enemyAtMine[i]->name,"Hammerguard") || !strcmp(enemyAtMine[i]->name,"Master")
		|| !strcmp(enemyAtMine[i]->name,"Berserker") || !strcmp(enemyAtMine[i]->name,"Scouter")
		|| !strcmp(enemyAtMine[i]->name,"Dragon") || !strcmp(enemyAtMine[i]->name,"Roshan"))
			if(enemyAtMine[i]->findBuff("Reviving") == NULL)
				cntEmeny++;
//		cout<<enemyAtMine[i]->name<<endl;
	}
	
	if (cntEmeny == 0 && dis2(worker->pos,MINE_POS[mineNow]) < 40 && strcmp(worker->name,"Scouter"))//杀光了,不是Scouter
	{
		int tm=mineNow;
		mineNow=mineNext;
		mineNext=tm;
		colSucceed=0;
	}

	if(!strcmp(worker->name,"Scouter") && cntEmeny == 0 && dis2(worker->pos,MINE_POS[mineNext]) < 140)
	{
		console->move(MINE_POS[mineNext]);
	}


	UnitFilter mfilter;
	mfilter.setAreaFilter (new Circle (worker->pos, 300 ), "a");
	vector<PUnit*> myHeroAtMine = console->friendlyUnits (mfilter);

	int cntMyHero=0;
	int cntMyScouter=0;

//	cout<<worker->name<<": ";
	for(int i=0;i<myHeroAtMine.size();i++)
	{
	//	cout<<myHeroAtMine[i]->name<<" ";
		if(!strcmp(myHeroAtMine[i]->name,"Hammerguard") || !strcmp(myHeroAtMine[i]->name,"Master")
		|| !strcmp(myHeroAtMine[i]->name,"Berserker") || !strcmp(myHeroAtMine[i]->name,"Scouter"))
			if(myHeroAtMine[i]->findBuff("Reviving") == NULL)
				cntMyHero++;
		if( !strcmp(myHeroAtMine[i]->name,"Scouter"))
			if(myHeroAtMine[i]->findBuff("Reviving") == NULL)	
				cntMyScouter++;
	}

//	cout<<worker->name<<" have S = "<<cntMyScouter<<endl;


	if(strcmp(worker->name,"Scouter") && dis2(worker->pos,MINE_POS[mineNext]) < 200 && cntMyScouter==0)//非Scouter
	{
		console->move(MINE_POS[mineNext]);
	}

//	cout<<"mineNow is "<<mineNow<<endl<<endl;
*/
}

int GoBackHome::countWorth()
{
	this->worth = 0;
	if(worker->hp <= goBackHomeHp && strcmp(worker->name, "Berserker") != 0) this->worth += goBackHomeArg;
	

	if(!strcmp(worker->name,"Scouter") )//是Scouter
	{
		this->worth = 0;
		UnitFilter efilter;
		efilter.setAreaFilter (new Circle (worker->pos, 300 ), "a");
		vector<PUnit*> enemyAtScouter = console->enemyUnits (efilter);
		int enemycnt=0;
		for(int i = 0; i < enemyAtScouter.size(); ++i)
		{
			if(!strcmp(enemyAtScouter[i]->name,"Hammerguard") || !strcmp(enemyAtScouter[i]->name,"Master")
			|| !strcmp(enemyAtScouter[i]->name,"Berserker") || !strcmp(enemyAtScouter[i]->name,"Scouter")
			|| !strcmp(enemyAtScouter[i]->name,"Dragon")|| !strcmp(enemyAtScouter[i]->name,"Roshan"))
				enemycnt++;
		}

		if(enemycnt!=0 && worker->findBuff("BeAttacked") != NULL && worker->hp <= (goBackHomeHp+30))
			this->worth += goBackHomeArg;
	}
	
	



	return this->worth;
}

void GoBackHome::work()
{
	console->selectUnit((const PUnit*)worker);
	console->move(console->getMilitaryBase()->pos);
}

int CallBackHome::countWorth()
{
	this->worth = 0;
	return this->worth;
}

void CallBackHome::work()
{
	console->callBackHero(worker);
}

void player_ai(const PMap &map, const PPlayerInfo &info, PCommand &cmd)
{

	srand(time(NULL));
	AIController controller(map,info,cmd);
	if(info.round == 0)
	{
		for(int i = 1; i <= 4; ++i)
			controller.buyNewHero();
		return;
	}
	if(enemyAttackHome == 0)
	{
		if(newHeroFirst)
		{
			controller.buyNewHero();
			controller.levelupHero();
		}
		else
		{
			controller.levelupHero();
			controller.buyNewHero();
		}
	}
	else
	{
		if(heroNum < 8)
			controller.buyNewHero();
		else
			controller.levelupHero();
	}
	controller.action();

//	cout<<"gatherSucceed: "<<gatherSucceed<<endl;
}