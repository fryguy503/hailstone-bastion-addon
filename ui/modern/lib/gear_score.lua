local C=dofile("lib/citadel_style.lua")
return function(p,s,scale)
  C.header("Item Ratings","Ratings are calculated by Bastion, including installed augments.",s.ready==1 and"Received"or"Waiting")
  C.card("gear-summary",0,0,function()
    C.section("Your equipment")
    C.field("Equipped item level",s.equipped or"Unavailable","gold")
    C.field("Best owned",s.owned or"Unavailable","good")
    C.note("Best owned includes eligible equipment received from the server. Stale or provisional labels are preserved until confirmed.","muted")
  end)
  C.section("Item details")
  C.note("Inspect an item to see its base rating and the rating of its installed augment configuration. Kinbound sharing and attunement information appears alongside the rating.")
  C.field("Received item templates",s.templates or 0)
  C.field("Received configurations",s.configurations or 0)
  if s.ready~=1 then
    C.note("Item ratings have not arrived.","warn")
    C.field("Negotiation attempts",(s.attempts or 0).." / 5")
  end
  if C.button("Retry item ratings",0,false,s.can_retry==1)then p.gear_score("retry")end
end
