"""Open IoT Platform - Auth Router."""
import logging
from fastapi import APIRouter, Depends, HTTPException, status
from fastapi.security import OAuth2PasswordRequestForm
from pydantic import BaseModel
from sqlalchemy.orm import Session
from database import get_db, User
from auth import hash_password, verify_password, create_access_token

logger = logging.getLogger("openiot.auth")

router = APIRouter(prefix="/api/auth", tags=["auth"])


class RegisterRequest(BaseModel):
    username: str
    email: str
    password: str
    display_name: str | None = None


class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    user: dict


@router.post("/register", response_model=TokenResponse)
async def register(req: RegisterRequest, db: Session = Depends(get_db)):
    """Register a new user."""
    try:
        if db.query(User).filter(User.username == req.username).first():
            raise HTTPException(400, "Username already taken")
        if db.query(User).filter(User.email == req.email).first():
            raise HTTPException(400, "Email already registered")

        user = User(
            username=req.username,
            email=req.email,
            hashed_password=hash_password(req.password),
            display_name=req.display_name or req.username,
        )
        db.add(user)
        db.commit()
        db.refresh(user)

        token = create_access_token({"sub": str(user.id)})
        return TokenResponse(
            access_token=token,
            user={
                "id": user.id,
                "username": user.username,
                "email": user.email,
                "display_name": user.display_name,
            },
        )
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Registration error: {e}", exc_info=True)
        raise HTTPException(500, f"Registration failed: {str(e)}")


@router.post("/login", response_model=TokenResponse)
async def login(
    form_data: OAuth2PasswordRequestForm = Depends(),
    db: Session = Depends(get_db),
):
    """Login with username/email and password."""
    try:
        user = db.query(User).filter(
            (User.username == form_data.username) | (User.email == form_data.username)
        ).first()

        if not user or not verify_password(form_data.password, user.hashed_password):
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Invalid credentials",
                headers={"WWW-Authenticate": "Bearer"},
            )

        token = create_access_token({"sub": str(user.id)})
        return TokenResponse(
            access_token=token,
            user={
                "id": user.id,
                "username": user.username,
                "email": user.email,
                "display_name": user.display_name,
            },
        )
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Login error: {e}", exc_info=True)
        raise HTTPException(500, f"Login failed: {str(e)}")
