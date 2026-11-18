# High speeds with the moteus controller - mjbots blog

URL: https://blog.mjbots.com/2020/07/08/high-speeds-with-the-moteus-controller/

Someone contacted me not too long ago who wanted to use the moteus controller, but wasn’t sure if it would be able to hit their target mechanical velocity of 6000rpm. I honestly wasn’t either, so I tested it. After a quick firmware fix, the devkit motor when run at 34V seems to be able to do it no problem.

      
    

It should be noted that the current firmware assumes you are within a thousand or so revolutions of 0. You can exceed that pretty quickly running at 120 revolutions per second!
