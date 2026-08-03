@page tut_first_actor First actor

# Billboards

In general a billboard is going to be a 3D plane with some kind of 2D graphic painted on to it. A quad and a graphic combined with other systems like particles is often more than enough to get the special effects done. Animating effects is just a matter of adjusting some physics variables and looping graphics. This was demonstrated in the first things tutorial.

For things that walk around maps or exist as props on the floor this kind of billboard when viewed from a higher or lower angle would look quite weird. In this tutorial we use engine provided mannequin assets for creating an actor.

# Actors

The name is also taken from Doom, though instead of Actors being a rigid class they are a thing that grants access to a character motor, collision, and eventually navigation agents when that is implemented. In a later tutorial we will cover "as-the-crow-flies" movement that mimic's doom bumping and shuffling around behavior.

## Mannequin

The 3D model and animations for the Mannequin were acquired at [Adobe Mixamo](https://www.mixamo.com) and then modified in Blender in an attempt to exagerrate the body proportions to make a prototyping sprite.


