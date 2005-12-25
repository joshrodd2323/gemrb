/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * $Header: /data/gemrb/cvs2svn/gemrb/gemrb/gemrb/plugins/Core/Inventory.cpp,v 1.64 2005/12/25 10:31:39 avenger_teambg Exp $
 *
 */

#include <stdio.h>
#include "../../includes/win32def.h"
#include "Interface.h"
#include "Inventory.h"
#include "Item.h"
#include "Actor.h"
#include "Game.h"

static int SLOT_MAGIC = -1;
static int SLOT_FIST = -1;
static int SLOT_MELEE = -1;

Inventory::Inventory()
{
	Owner = NULL;
	InventoryType = INVENTORY_HEAP;
	Changed = false;
	Weight = 0;
	Equipped = IW_NO_EQUIPPED;
}

Inventory::~Inventory()
{
	for (size_t i = 0; i < Slots.size(); i++) {
		if (Slots[i]) {
			delete( Slots[i] );
			Slots[i] = NULL;
		}
	}
}

CREItem *Inventory::GetItem(size_t idx)
{
	CREItem *item = Slots[idx];
	Slots.erase(Slots.begin()+idx);
	return item;
}

void Inventory::AddItem(CREItem *item)
{
	if (!item) return; //invalid items get no slot
	Slots.push_back(item);
	//Changed=true; //probably not needed, chests got no encumbrance
}

void Inventory::CalculateWeight()
{
	if (!Changed) {
		return;
	}
	Weight = 0;
	for (size_t i = 0; i < Slots.size(); i++) {
		CREItem *slot = Slots[i];
		if (!slot) {
			continue;
		}
		//printf ("%2d: %8s : %d x %d\n", (int) i, slot->ItemResRef, slot->Weight, slot->Usages[0]);
		if (slot->Weight == -1) {
			Item *itm = core->GetItem( slot->ItemResRef );
			if (itm) {
				//simply adding the item flags to the slot
				slot->Flags |= (itm->Flags<<8);
				//some slot flags might be affected by the item flags
				if (!(slot->Flags & IE_INV_ITEM_CRITICAL)) {
					slot->Flags |= IE_INV_ITEM_DESTRUCTIBLE;
				}
				if (!(slot->Flags & IE_INV_ITEM_MOVABLE)) {
					slot->Flags |= IE_INV_ITEM_UNDROPPABLE;
				}
				//this is not correct, cursed items are droppable if unwielded
				//the consolidated flags will help this, new flag: IE_INV_ITEM_CURSED
				//if (itm->Flags & IE_ITEM_CURSED) {
				//	slot->Flags |= IE_INV_ITEM_UNDROPPABLE;
				//}
				
				if (slot->Flags & IE_INV_ITEM_STOLEN2) {
					slot->Flags |= IE_INV_ITEM_STOLEN;
				}
				
				slot->Weight = itm->Weight;
				slot->StackAmount = itm->StackAmount;
				core->FreeItem( itm, slot->ItemResRef, false );
			}
			else {
				printMessage( "Inventory", " ", LIGHT_RED);
				printf("Invalid item: %s!\n", slot->ItemResRef);
				slot->Weight = 0;
			}
		} else {
			slot->Flags &= ~IE_INV_ITEM_ACQUIRED;
		}
		if (slot->Weight > 0) {
			Weight += slot->Weight * ((slot->Usages[0] && slot->StackAmount > 1) ? slot->Usages[0] : 1);
		}
	}
	Changed = false;
}

void Inventory::AddSlotEffects(CREItem* slot)
{
	Item* itm = core->GetItem( slot->ItemResRef );
	if (!itm) {
		printMessage("Inventory","Invalid item equipped...\n",LIGHT_RED);
		return;
	}
	for (int i = 0; i < itm->EquippingFeatureCount; i++) {
		Owner->fxqueue.AddEffect( &itm->equipping_features[i] );
	}
}

void Inventory::RemoveSlotEffects(CREItem* slot)
{
	if (!slot)
		return;
	Item* itm = core->GetItem( slot->ItemResRef );
	if (!itm)
		return;
	for (int i = 0; i < itm->EquippingFeatureCount; i++) {
		Effect* fx = &itm->equipping_features[i];
		if (fx->TimingMode == FX_DURATION_INSTANT_WHILE_EQUIPPED) {
			Owner->fxqueue.RemoveEffect( fx );
		}
	}
}


void Inventory::SetInventoryType(int arg)
{
	InventoryType = arg;
}

void Inventory::SetSlotCount(unsigned int size)
{
	if (Slots.size()) {
		printf("Inventory size changed???\n");
		//we don't allow reassignment,
		//if you want this, delete the previous Slots here
		abort(); 
	}
	Slots.assign((size_t) size, NULL);
}

/** if you supply a "" string, then it checks if the slot is empty */
bool Inventory::HasItemInSlot(const char *resref, unsigned int slot)
{
	if (slot>=Slots.size()) {
		return false;
	}
	CREItem *item = Slots[slot];
	if (!item) {
		if (resref[0]) {
			return false;
		}
		return true;
	}
	if (strnicmp( item->ItemResRef, resref, 8 )==0) {
		return true;
	}
	return false;
}

/** counts the items in the inventory, if stacks == 1 then stacks are 
		accounted for their heap size */
int Inventory::CountItems(const char *resref, bool stacks)
{
	int count = 0;
	int slot = Slots.size();
	while(slot--) {
		CREItem *item = Slots[slot];
		if (!item) {
			continue;
		}
		if (resref && resref[0]) {
			if (!strnicmp(resref, item->ItemResRef, 8) )
				continue;
		}
		if (stacks && (item->Flags&IE_INV_ITEM_STACKED) ) {
			count+=item->Usages[0];
		}
		else {
			count++;
		}
	}
	return count;
}

/** this function can look for stolen, equipped, identified, destructible
		etc, items. You just have to specify the flags in the bitmask
		specifying 1 in a bit signifies a requirement */
bool Inventory::HasItem(const char *resref, ieDword flags)
{
	int slot = Slots.size();
	while(slot--) {
		CREItem *item = Slots[slot];
		if (!item) {
			continue;
		}
		if ( (flags&item->Flags)!=flags) {
				continue;
		}
		if (resref[0] && strnicmp(item->ItemResRef, resref,8) ) {
			continue;
		}
		return true;
	}
	return false;
}

void Inventory::KillSlot(unsigned int index)
{
	if (InventoryType==INVENTORY_HEAP) {
		Slots.erase(Slots.begin()+index);
	} else {
		RemoveSlotEffects( GetSlotItem(index) );
		Slots[index] = NULL;
	}
}
/** if resref is "", then destroy ALL items
this function can look for stolen, equipped, identified, destructible
etc, items. You just have to specify the flags in the bitmask
specifying 1 in a bit signifies a requirement */
unsigned int Inventory::DestroyItem(const char *resref, ieDword flags, ieDword count)
{
	unsigned int destructed = 0;
	unsigned int slot = Slots.size();
	while(slot--) {
		CREItem *item = Slots[slot];
		if (!item) {
			continue;
		}
		// here you can simply destroy all items of a specific type
		if ( (flags&item->Flags)!=flags) {
				continue;
		}
		if (resref[0] && strnicmp(item->ItemResRef, resref, 8) ) {
			continue;
		}
		//we need to acknowledge that the item was destroyed
		//use unequip stuff, decrease encumbrance etc,
		//until that, we simply erase it
		ieDword removed;

		if (item->Flags&IE_INV_ITEM_STACKED) {
			removed=item->Usages[0];
			if (count && (removed + destructed > count) ) {
				removed = count - destructed;
				item = RemoveItem( slot, removed );
			}
			else {
				KillSlot(slot);
			}
		} else {
			removed=1;
			KillSlot(slot);
		}
		delete item;
		Changed = true;
		destructed+=removed;
		if (count && (destructed>=count) )
			break;
	}

	return destructed;
}

CREItem *Inventory::RemoveItem(unsigned int slot, unsigned int count)
{
	CREItem *item;

	if (slot>=Slots.size() ) {
		printMessage("Inventory","Invalid slot!\n",LIGHT_RED);
		abort();
	}
	Changed = true;
	item = Slots[slot];
	if (!count || !(item->Flags&IE_INV_ITEM_STACKED) ) {
		KillSlot(slot);
		return item;
	}
	if (count >= item->Usages[0]) {
		KillSlot(slot);
		return item;
	}

	CREItem *returned = new CREItem(*item);
	item->Usages[0]-=count;
	returned->Usages[0]=count;
	return returned;
}

int Inventory::RemoveItem(const char *resref, unsigned int flags, CREItem **res_item)
{
	int slot = Slots.size();
	while(slot--) {
		CREItem *item = Slots[slot];
		if (!item) {
			continue;
		}
		if ( ((flags^IE_INV_ITEM_UNDROPPABLE)&item->Flags)!=flags) {
				continue;
		}
		if (resref[0] && strnicmp(item->ItemResRef, resref, 8) ) {
			continue;
		}
		*res_item=RemoveItem(slot, 0);
		return slot;
	}
	*res_item = NULL;
	return -1;
}

void Inventory::SetSlotItem(CREItem* item, unsigned int slot)
{
	if (slot>=Slots.size() ) {
		printMessage("Inventory","Invalid slot!\n",LIGHT_RED);
		abort();
	}
	Changed = true;
	if (Slots[slot]) {
		delete Slots[slot];
	}
	Slots[slot] = item;
}

int Inventory::AddSlotItem(CREItem* item, int slot)
{
	if (slot >= 0) {
		if ((unsigned)slot >= Slots.size()) {
			printMessage("Inventory","Invalid slot!\n",LIGHT_RED);
			abort();
		}

		if (!Slots[slot]) {
			item->Flags |= IE_INV_ITEM_ACQUIRED;
			Slots[slot] = item;
			Changed = true;
			return 2;
		}

		CREItem *myslot = Slots[slot];
		if (ItemsAreCompatible( myslot, item )) {			
 			//calculate with the max movable stock
			int chunk = item->Usages[0];
			int newamount = myslot->Usages[0]+chunk;
			if (newamount>myslot->StackAmount) {
				newamount=myslot->StackAmount;
				chunk = item->Usages[0]-newamount;
			}
			if (!chunk) {
				return -1;
			}	
			myslot->Flags |= IE_INV_ITEM_ACQUIRED;
			myslot->Usages[0] += chunk;
			item->Usages[0] -= chunk;
			Changed = true;
			if (item->Usages[0] == 0) {
				delete item;
				return 2;
			}
			else {
				return 1;
			}
		}
		return 0;
	}

	int res = 0;
	for (size_t i = 0; i<Slots.size(); i++) {
		//looking for default inventory slot (-1)
		if (core->QuerySlotType(i) != -1) {
			continue;
		}
		int part_res = AddSlotItem (item, i);
		if (part_res == 2) return 2;
		else if (part_res == 1) res = 1;
	}

	return res;
}

int Inventory::AddStoreItem(STOItem* item, int action)
{
	CREItem *temp;
	int ret = -1;

	// item->PurchasedAmount is the number of items bought
	// (you can still add grouped objects in a single step,
	// just set up STOItem)
	for (int i = 0; i < item->PurchasedAmount; i++) {
		if (item->InfiniteSupply==(ieDword) -1) {
			if (!item->AmountInStock) {
				break;
			}
			item->AmountInStock--;
		}

		//the first part of a STOItem is essentially a CREItem
		temp = new CREItem();
		memcpy( temp, item, sizeof( CREItem ) ); 
		if (action==STA_STEAL) {
			temp->Flags |= IE_INV_ITEM_STOLEN;
		}

		ret = AddSlotItem( temp, -1 );
		if (ret != 2) {
			//FIXME: drop remains at feet of actor
			delete temp;
		}
	}
	item->PurchasedAmount = 0;
	return ret;
}

/* could the source item be dropped on the target item to merge them */
bool Inventory::ItemsAreCompatible(CREItem* target, CREItem* source)
{
	if (!target) {
		//this isn't always ok, please check!
		printMessage("Inventory","Null item encountered by ItemsAreCompatible()",YELLOW);
		return true;
	}

	if (!(source->Flags&IE_INV_ITEM_STACKED) ) {
		return false;
	}

	if (!strnicmp( target->ItemResRef, source->ItemResRef,8 )) {
		return true;
	}
	return false;
}

int Inventory::FindItem(const char *resref, unsigned int flags)
{
	for (size_t i = 0; i < Slots.size(); i++) {
		CREItem *item = Slots[i];
		if (!item) {
			continue;
		}
		if ( ((flags^IE_INV_ITEM_UNDROPPABLE)&item->Flags)!=flags) {
				continue;
		}
		if (resref[0] && strnicmp(item->ItemResRef, resref, 8) ) {
			continue;
		}
		return i;
	}
	return -1;
}

void Inventory::DropItemAtLocation(unsigned int slot, unsigned int flags, Map *map, Point &loc)
{
	if (slot>Slots.size()) {
		return;
	}
	CREItem *item = Slots[slot];
	if (!item) {
		return;
	}
	//if you want to drop undoppable items, simply set IE_INV_UNDROPPABLE
	//by default, it won't drop them
	if ( ((flags^IE_INV_ITEM_UNDROPPABLE)&item->Flags)!=flags) {
		return;
	}
	map->AddItemToLocation(loc, item);
	Changed = true;
	Slots[slot]=NULL;
}

void Inventory::DropItemAtLocation(const char *resref, unsigned int flags, Map *map, Point &loc)
{
	//this loop is going from start
	for (size_t i = 0; i < Slots.size(); i++) {
		CREItem *item = Slots[i];
		if (!item) {
			continue;
		}
		//if you want to drop undoppable items, simply set IE_INV_UNDROPPABLE
		//by default, it won't drop them
		if ( ((flags^IE_INV_ITEM_UNDROPPABLE)&item->Flags)!=flags) {
				continue;
		}
		if (resref[0] && strnicmp(item->ItemResRef, resref, 8) ) {
			continue;
		}
		map->AddItemToLocation(loc, item);
		Changed = true;
		Slots[i]=NULL;
		//if it isn't all items then we stop here
		if (resref[0])
			break;
	}
}

CREItem *Inventory::GetSlotItem(unsigned int slot)
{
	if (slot>=Slots.size() ) {
		printMessage("Inventory","Invalid slot!\n",LIGHT_RED);
		abort();
	}
	return Slots[slot];
}

//this is the low level equipping
//all checks have been made previously
//if weapon is set, then equipitem is equipping a weapon too
bool Inventory::EquipItem(unsigned int slot, bool weapon)
{
	CREItem *item = GetSlotItem(slot);
	if (!item) {
		return false;
	}
	// add effects of an item just being equipped to actor's effect queue
	int effect = core->QuerySlotEffects( slot );
	if (effect) {
		if ((effect==1) || weapon) {
			item->Flags|=IE_INV_ITEM_EQUIPPED;
			if (item->Flags & IE_INV_ITEM_CURSED) {
				item->Flags|=IE_INV_ITEM_UNDROPPABLE;
			}
			AddSlotEffects( item );
		}
	}
	return true;
}

//the removecurse flag will check if it is possible to move the item to the inventory
//after a remove curse spell
bool Inventory::UnEquipItem(unsigned int slot, bool removecurse)
{
	CREItem *item = GetSlotItem(slot);
	if (!item) {
		return false;
	}
	if (removecurse) {
		if (item->Flags & IE_INV_ITEM_MOVABLE) {
			item->Flags&=~IE_INV_ITEM_UNDROPPABLE;
		}
		if (FindCandidateSlot(-1,0,item->ItemResRef)<0) {
			return false;
		}
	}
	if (item->Flags & IE_INV_ITEM_UNDROPPABLE) {
		return false;
	}
	item->Flags &= ~IE_INV_ITEM_EQUIPPED;
	if (core->QuerySlotEffects( slot )) {
		RemoveSlotEffects( item );
	}
	return true;
}

// find which bow is attached to the projectile marked by 'Equipped'
int Inventory::FindRanged()
{
	return SLOT_MELEE;
}

void Inventory::SetFistSlot(int arg) { SLOT_FIST=arg; }
void Inventory::SetMagicSlot(int arg) { SLOT_MAGIC=arg; }
void Inventory::SetWeaponSlot(int arg)
{
	if (SLOT_MELEE==-1) {
		SLOT_MELEE=arg;
	}
}

int Inventory::GetFistSlot()
{
	return SLOT_FIST;
}

int Inventory::GetMagicSlot()
{
	return SLOT_MAGIC;
}

int Inventory::GetWeaponSlot()
{
	return SLOT_MELEE;
}

int Inventory::GetEquipped()
{
	return Equipped;
}

int Inventory::GetEquippedSlot()
{
	if (Equipped==IW_NO_EQUIPPED) {
		return SLOT_FIST;
	}
	return Equipped+SLOT_MELEE;
}

void Inventory::SetEquippedSlot(int slotcode)
{
	if (slotcode == IW_NO_EQUIPPED) {
		Equipped = IW_NO_EQUIPPED;
		return;
	}
	if (HasItemInSlot("",slotcode+SLOT_MELEE)) {
		Equipped = IW_NO_EQUIPPED;
		return;
	}
	Equipped = slotcode;
}

CREItem *Inventory::GetUsedWeapon()
{
	CREItem *ret;
	int slot;

	if (SLOT_MAGIC!=-1) {
		ret = GetSlotItem(SLOT_MAGIC);
		if (ret && ret->ItemResRef[0]) {
			return ret;
		}
	}
	slot = GetEquippedSlot();
	return GetSlotItem(slot);
}

// Returns index of first empty slot or slot with the same
// item and not full stack. On fail returns -1
// Can be used to check for full inventory
int Inventory::FindCandidateSlot(int slottype, size_t first_slot, const char *resref)
{
	if (first_slot >= Slots.size())
		return -1;

	for (size_t i = first_slot; i < Slots.size(); i++) {
		CREItem *item = Slots[i];

		if (!(core->QuerySlotType(i)&slottype) ) {
			continue;
		}

		if (!item) {
			return i; //this is a good empty slot
		}
		if (!resref) {
			continue;
		}
		if (!(item->Flags&IE_INV_ITEM_STACKED) ) {
			continue;
		}
		if (strnicmp( item->ItemResRef, resref, 8 )!=0) {
			continue;
		}
		// check if the item fits in this slot, we use the cached
		// stackamount value
		if (item->Usages[0]<item->StackAmount)
			return i;
	}

	return -1;
}

void Inventory::SetSlotItemRes(const ieResRef ItemResRef, int SlotID, int Charge0, int Charge1, int Charge2)
{
	CREItem *TmpItem = new CREItem();
	strnlwrcpy(TmpItem->ItemResRef, ItemResRef, 8);
	TmpItem->PurchasedAmount=0;
	TmpItem->Usages[0]=Charge0;
	TmpItem->Usages[1]=Charge1;
	TmpItem->Usages[2]=Charge2;
	TmpItem->Flags=IE_INV_ITEM_ACQUIRED;
	core->ResolveRandomItem(TmpItem);
	AddSlotItem( TmpItem, SlotID );
}

void Inventory::dump()
{
	printf( "INVENTORY:\n" );
	for (unsigned int i = 0; i < Slots.size(); i++) {
		CREItem* itm = Slots[i];

		if (!itm) {
			continue;
		}

		printf ( "%2u: %8.8s - (%d %d %d) %x Wt: %d x %dLb\n", i, itm->ItemResRef, itm->Usages[0], itm->Usages[1], itm->Usages[2], itm->Flags, itm->StackAmount, itm->Weight );
	}

	printf( "Equipped: %d\n", Equipped );
	Changed = true;
	CalculateWeight();
	printf( "Total weight: %d\n", Weight );
}
